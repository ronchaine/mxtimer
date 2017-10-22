#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

#define MUSH_MAKE_IMPLEMENTATIONS

#include <mush/string.hpp>
#include <mush/buffer.hpp>

#define MUSH_FREETYPE_FONTS
#define MUSH_BITMAP_FONTS
#include <mush/font.hpp>

#include <mush/extra/opengl.hpp>
#include <mush/configuration.hpp>

namespace mxgl = mush::extra::opengl;

#include "stb_image.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <sstream>
#include <fstream>
#include <unordered_map>

GLFWwindow* window;

using Vertex = mxgl::VertexType<2, 1, mxgl::VERTEX_RGBA_COLOUR | mxgl::VERTEX_HSV_SHIFT>;
using VertexNT = mxgl::VertexType<2, 0, mxgl::VERTEX_RGBA_COLOUR | mxgl::VERTEX_HSV_SHIFT>;

using ClockType = std::chrono::steady_clock;

void window_size_changed(GLFWwindow* window, int w, int h)
{
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    mxgl::update_screen_size(w, h);
}

int init_gl()
{
    // init GL in whatever way you like, here we use glfw and libepoxy
    // glad version commented out
    if (!glfwInit())
    {
        std::cout << "failed glfw init\n";
        return -1;
    }
 
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(250, 400, "mxsplit timer", nullptr, nullptr);

    if (!window)
    {
        glfwTerminate();
        std::cout << "couldn't open window or init gl\n";
        return -1;
    }
    
    glfwMakeContextCurrent(window); 
    glfwSetWindowSizeCallback(window, window_size_changed);

    // Setup GL state
    GLuint VertexArrayID;
    glGenVertexArrays(1, &VertexArrayID);
    glBindVertexArray(VertexArrayID);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    mxgl::update_screen_size(800, 600);

    int major, minor, rev;
    major = glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MAJOR);
    minor = glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MINOR);
    rev = glfwGetWindowAttrib(window, GLFW_CONTEXT_REVISION);
    printf("OpenGL version: %d.%d.%d\n", major, minor, rev);
    printf("GL version string: %s\n", (const char*)glGetString(GL_VERSION));
    printf("GLSL version string: %s\n", (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
    
    // ok, we're done, this is from where the stuff we're interested in starts
    return 0; 
}

inline mush::string time_to_string(const std::chrono::milliseconds& ms)
{
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << std::chrono::duration_cast<std::chrono::hours>(ms).count() << ":" 
       << std::setfill('0') << std::setw(2) << std::chrono::duration_cast<std::chrono::minutes>(ms).count() % 60 << ":"
       << std::setfill('0') << std::setw(2) << std::chrono::duration_cast<std::chrono::seconds>(ms).count() % 60 << "."
       << std::setfill('0') << std::setw(2) << (ms.count() % 1000) / 10;

    return ss.str();
}

template <typename T>
struct BufferedShader
{
    mxgl::VertexBuffer<T>   vertices;
    mxgl::Shader<T>         shader;
};

bool alive = true;

template <typename Clock>
class Timer
{
    private:
        typename Clock::time_point      start_time;
        std::chrono::milliseconds       accumulated;

        Clock                           clock;
        bool                            active;

    public:
        Timer()
        {
            static_assert(Clock::is_steady, "Steady clock required for timing");
            active = false;
            reset();
        }
        bool running() const
        {
            return active;
        }
        void start()
        {
            if (active)
                return;
            active = true;
            start_time = clock.now();
        }
        void stop()
        {
            if (!active)
                return;
            active = false;
            accumulated += std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - start_time);
        }
        void reset()
        {
            accumulated = std::chrono::milliseconds(0);
        }

        std::chrono::milliseconds current() const
        {
            if (active)
                return std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - start_time)
                     + accumulated;
            else
                return accumulated;
        }
};

struct Records
{
    std::chrono::milliseconds                       best;
    std::chrono::milliseconds                       average;

    uint32_t                                        attempts;
};

class Split
{
    private:
    public:
        mush::string                                image;
        mush::string                                name;

        std::vector<mush::string>                   shortcuts;

        Timer<ClockType>                            timer;

        Records                                     records;

        friend inline std::ostream& operator<<(std::ostream& out, const Split& split)
        {
            out << "Split name:     " << split.name << "\n";
            out << "Image:          " << split.image << "\n";
            out << "Timer active:   " << (split.timer.running() == true ? "yes" : "no") << "\n";
            out << "Current time:   " << time_to_string(split.timer.current()) << "\n";
            out << "Best time:      " << time_to_string(split.records.best) << "\n";
            out << "Average time:   " << time_to_string(split.records.average) << "\n";
            out << "Attempts:       " << split.records.attempts << "\n";

            return out;
        }
};

std::chrono::milliseconds get_ms(const mush::String& str)
{
    if (str.length() == 1)
        return std::chrono::milliseconds(str.to_value<uint32_t>() * 100);
    else if (str.length() == 2)
        return std::chrono::milliseconds(str.to_value<uint32_t>() * 10);
    else if (str.length() == 3)
        return std::chrono::milliseconds(str.to_value<uint32_t>());

    return std::chrono::milliseconds(0);
}

std::chrono::milliseconds string_to_time_in_ms(const mush::String& str)
{
    std::chrono::milliseconds rval(0);

    std::vector<mush::String> parts = str.split(":");
    uint32_t hours, minutes, seconds;

    bool has_milliseconds = false;

    std::vector<mush::String> sparts = str.split(".");
    if (sparts.size() == 2)
        has_milliseconds = true;

    if (parts.size() == 3)
    {
        hours   = parts[0].to_value<uint32_t>();
        minutes = parts[1].to_value<uint32_t>();
        seconds = parts[2].split('.')[0].to_value<uint32_t>();
        if (has_milliseconds)
            rval = get_ms(parts[2].split('.')[1]);
    }
    else if (parts.size() == 2)
    {
        hours   = 0;
        minutes = parts[0].to_value<uint32_t>();
        seconds = parts[1].split('.')[0].to_value<uint32_t>();
        if (has_milliseconds)
            rval = get_ms(parts[1].split('.')[1]);
    }
    else if (parts.size() == 1)
    {
        hours   = 0;
        minutes = 0;
        seconds = parts[0].split('.')[0].to_value<uint32_t>();
        if (has_milliseconds)
            rval = get_ms(parts[0].split('.')[1]);
    }

    uint32_t total_seconds = seconds + ((minutes + (hours * 60)) * 60);

    rval += std::chrono::milliseconds(total_seconds * 1000);
    return rval;
}

class SplitTimer
{
    private:
        BufferedShader<Vertex>      textured;
        BufferedShader<VertexNT>    untextured;

        std::deque<Split> splits;
        std::unordered_map<mush::string, size_t> split_index;
        std::unordered_map<mush::string, std::unique_ptr<mush::Font<mush::RGBA, mush::FREETYPE_FONT>>> fonts;

        mxgl::SpriteSheet<4>            sprites;
        mxgl::console::Console<Vertex>  console;

        mush::Configuration             conf;

        Records                         record;

        std::unique_ptr<std::thread>    command;

    public:
        SplitTimer() : console(textured.vertices, sprites)
        {
            textured.shader.generate_default();
            untextured.shader.generate_default();
            
            fonts["return_of_ganon_32"] = std::make_unique<mush::Font<mush::RGBA, mush::FREETYPE_FONT>>(mush::load_freetype("ReturnofGanon.ttf", 32));
            fonts["return_of_ganon_16"] = std::make_unique<mush::Font<mush::RGBA, mush::FREETYPE_FONT>>(mush::load_freetype("ReturnofGanon.ttf", 16));
        }
        ~SplitTimer()
        {
            command->join();
        }

        bool running()
        {
            for (Split& s : splits)
                if (s.timer.running())
                    return true;
            return false;
        }

        std::chrono::milliseconds total_time()
        {
            std::chrono::milliseconds total(0);

            for (Split& s : splits)
                total += s.timer.current();

            return total;
        }
        std::chrono::milliseconds total_average()
        {
            std::chrono::milliseconds total(0);

            for (Split& s : splits)
                total += s.records.average;

            return total;
        }
        
        std::chrono::milliseconds total_best()
        {
            std::chrono::milliseconds total(0);

            for (Split& s : splits)
                total += s.records.best;

            return total;
        }

        std::chrono::milliseconds render_interval()
        {
            if (conf["general.render_wait"] == "")
                return std::chrono::milliseconds(0);
            else
                return std::chrono::milliseconds(conf["general.render_wait"].to_value<uint32_t>());
        }

        void draw_split(size_t index, uint32_t x, uint32_t y)
        {
            if (index >= splits.size() || index < 0)
                return;

            console.set_font(*fonts["return_of_ganon_32"]);
            console.set_origin(x + 10, y + 16);
            console.reset_position();
            console.write(time_to_string(splits[index].timer.current()));
            console.set_font(*fonts["return_of_ganon_16"]);
            console.set_cursor(x + 10, y + 50);
            console.write("avg:  ", time_to_string(splits[index].records.average), "\n");
            console.write("best: ", time_to_string(splits[index].records.best));
            console.set_cursor(x + 10, y + 2);
            console.write(splits[index].name);
            mxgl::draw::sprite(textured.vertices, sprites[splits[index].image], x + 140, y + 7, 77, 77, 0xffffffff);
            mxgl::draw::rectangle(untextured.vertices, x, y, 230, 91, 0x50505050);
        }

        void draw_split_small(size_t index, uint32_t x, uint32_t y)
        {
            console.set_font(*fonts["return_of_ganon_16"]);
            console.set_origin(x + 10, y + 1);
            console.reset_position();
            console.write(splits[index].name);
            console.set_cursor(x + 135, y + 1);
            console.write(time_to_string(splits[index].timer.current()));
            mxgl::draw::rectangle(untextured.vertices, x, y, 230, 20, 0x50505050);
            console.set_cursor(x + 190, y + 1);
            console.write("-10:32");
        }

        void draw_total(uint32_t x, uint32_t y)
        {
            console.set_font(*fonts["return_of_ganon_16"]);
            console.set_origin(x + 10, y + 1);
            console.reset_position();
            console.write("Total time:");
            console.set_font(*fonts["return_of_ganon_32"]);
            console.write("  ", time_to_string(total_time()));
            console.set_font(*fonts["return_of_ganon_16"]);
            console.set_font(*fonts["return_of_ganon_16"]);
            console.set_cursor(x + 10, y + 40);
            console.write("avg:  ", time_to_string(total_average()), "\n");
            console.write("best: ", time_to_string(total_best()));
            mxgl::draw::rectangle(untextured.vertices, x, y, 230, 80, 0x50505050);
        }

        void draw_splits(uint32_t amount = 10)
        {
            textured.vertices.clear();
            untextured.vertices.clear();
            draw_split(0, 10, 10);
            
            for (size_t i = 1; i < splits.size(); ++i)
            {
                if (i >= amount)
                    break;
                draw_split_small(i, 10, 105 + 23 * (i-1));
            }

            draw_total(10, 313);

            mxgl::render_buffers(untextured.vertices, untextured.shader);
            mxgl::render_buffers(textured.vertices, textured.shader);
        }

        size_t get_split_index(const mush::string& name)
        {
            size_t index = ~0;
            
            if (split_index.count(name) == 0)
            {
                for (size_t i = 0; i < splits.size(); ++i)
                    if (splits[i].name == name)
                    {
                        index = i;
                        break;
                    }
            } else
                index = split_index[name];

            return index;
        }

        void move_to_top(const mush::String& name)
        {
            size_t index = get_split_index(name);

            if (index == ~0)
            {
                std::cout << "no match for split " << name << "\n";
                return;
            }

            splits.push_front(Split());
            splits[0] = splits[index+1];
            for (size_t i = index+1; i < splits.size()-1; ++i)
                splits[i] = splits[i+1];
            splits.pop_back();
        }

        void move_to_top(size_t index)
        {
            splits.push_front(Split());
            splits[0] = splits[index+1];
            for (size_t i = index+1; i < splits.size()-1; ++i)
                splits[i] = splits[i+1];
            splits.pop_back();
        }
        
        void switch_to_split(const mush::string& split_name)
        {
            size_t index = get_split_index(split_name);
            std::cout << "Switching active split to '" << split_name << "'\n";
            if (splits[0].timer.running() == true)
            {
                splits[index].timer.start();
                splits[0].timer.stop();
            }
            move_to_top(index);
        }

        void list_splits()
        {
            for (Split& s : splits)
                std::cout << s << "\n";
        }

        void make_splits()
        {
            splits.clear();
            split_index.clear();

            std::vector<mush::String> parsed_option;
            for (auto& opt : conf)
            {
                parsed_option = opt.name.split(".");
                if (parsed_option.size() == 1)
                    continue;

                if (parsed_option[0] == "split")
                {
                    if (split_index.count(parsed_option[1]) == 0)
                    {
                        split_index[parsed_option[1]] = splits.size();
                        splits.emplace_back(Split());
                    }

                    if (parsed_option.size() < 2)
                        continue;

                    Split& mod = splits[split_index[parsed_option[1]]];
                    
                    if (parsed_option[2] == "name")
                        mod.name = opt.value;
                    else if (parsed_option[2] == "image")
                    {
                        mod.image = opt.value;
                        if (!sprites.has(opt.value))
                            mxgl::file_to_spritesheet(sprites, opt.value.std_str().c_str());
                    }
                    else if (parsed_option[2] == "short")
                        mod.shortcuts = opt.value.split(",");
                    else if (parsed_option[2] == "best")
                        mod.records.best = string_to_time_in_ms(opt.value);
                    else if (parsed_option[2] == "average")
                        mod.records.average = string_to_time_in_ms(opt.value);
                    else if (parsed_option[2] == "attempts")
                        mod.records.attempts = opt.value.to_value<uint32_t>();
                }
            }
        }

        void load(const char* filename)
        {
            bool start_console = false;

            std::fstream file(filename, std::fstream::in);
            conf = mush::load_ini(file);
            file.close();
            
            if (conf["general.record_file"] != "")
            {
                file.open(conf["general.record_file"], std::fstream::in);
                if (file.is_open())
                    conf.load_from_ini(file);
                file.close();
            }

            make_splits();

            if (conf["general.use_console"] == "true")
            {
                std::cout << "console requested\n";
                command = std::make_unique<std::thread>(&SplitTimer::cmd, this);
            }
        }

        void save(const char* filename)
        {
            std::fstream file(filename, std::fstream::out | std::fstream::trunc);
            for (mush::Option& opt : conf)
            {
            }
            file.close();
        }

        void cmd()
        {
            std::cout << "console ready\n>" << std::flush;
            std::string input;
            while(true)
            {
                std::cin >> input;
                glfwPostEmptyEvent();

                // internal commands
                if (input == "quit")
                {
                    alive = false;
                    break;
                }
                else if (input == "start")
                {
                    splits[0].timer.start();
                }
                else if (input == "stop")
                {
                    splits[0].timer.stop();
                }
                else if (input == "reset")
                {
                    for (size_t i = 0; i < splits.size(); ++i)
                    {
                        splits[i].timer.stop();
                        splits[i].timer.reset();
                    }
                    make_splits();
                }
                else if (input == "save")
                {
                    //TODO: SAVE
                }
                else if (input == "window_size")
                {
                    std::cout << mxgl::screen_width() << "x" << mxgl::screen_height << "\n";
                }
                else
                {
                    // split names / shortcuts
                    for (size_t i = 0; i < splits.size(); ++i)
                    {
                        if (splits[i].name == mush::string(input))
                        {
                            switch_to_split(splits[i].name);
                            std::cout << ">" << std::flush;
                            continue;
                        }

                        for (auto& sc : splits[i].shortcuts)
                            if (mush::string(input) == sc)
                            {
                                switch_to_split(splits[i].name);
                                std::cout << ">" << std::flush;
                                continue;
                            }
                    }
                }
                std::cout << "Unknown command\n>" << std::flush;
            }
        }
};

int main()
{
    init_gl();

    SplitTimer st;
    st.load("alttprando.ini");

    while(!glfwWindowShouldClose(window) && alive)
    {
        glClear(GL_COLOR_BUFFER_BIT);

        st.draw_splits();

        glfwSwapBuffers(window);
        if (st.running())
        {
            glfwPollEvents();
            std::this_thread::sleep_for(st.render_interval());
        }
        else
            glfwWaitEvents();
    }

    return 0;
}
