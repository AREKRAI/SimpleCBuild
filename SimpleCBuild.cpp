#include <iostream>

#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <fstream>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <windows.h>

#define BIT(x) 1 << x

// From: https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
template<typename ... Args>
std::string stringFormat( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'

    if( size_s <= 0 )
        throw std::runtime_error( "Error during formatting.");

    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf(buf.get(), size, format.c_str(), args ...);
    return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

int isFile(const char *path)
{
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

std::string getCurrentDirectory() 
{
    TCHAR buffer[MAX_PATH] = { 0 };
    GetModuleFileName( NULL, buffer, MAX_PATH );
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
}

template<typename T, typename P>
T remove_if(T beg, T end, P pred)
{
    T dest = beg;
    for (T itr = beg;itr != end; ++itr)
        if (!pred(*itr))
            *(dest++) = *itr;
    return dest;
}

enum class ProjectFlags
{
    None = 0,
    Debug = BIT(1),
    SharedLib = BIT(2),
    StaticLib = BIT(3)
};

ProjectFlags operator|(const ProjectFlags& first, const ProjectFlags& second)
{
    return static_cast<ProjectFlags>(static_cast<uint64_t>(first) | static_cast<uint64_t>(second));
}

uint64_t operator&(const ProjectFlags& first, const ProjectFlags& second)
{
    return static_cast<uint64_t>(first) & static_cast<uint64_t>(second);
}

class Project
{
public:
    Project()=default;

    Project(const std::vector<std::string>& args)
    {
        m_stringFlags.push_back("-Wall");
        // m_stringFlags.push_back("--verbose");

        for (const std::string& arg : args)
        {
            if (arg == "-debug")
            {
                m_flags = m_flags | ProjectFlags::Debug;
                m_stringFlags.push_back("-g");
            }
        }
    }

    ~Project()=default;

    void parse(const std::string_view& fileName)
    {
        std::ifstream file;
        file.open(fileName.data());

        if (file.is_open())
        {
            for (std::string line; std::getline(file, line);)
            {
                line.erase(remove_if(line.begin(), line.end(), isspace), line.end()); // strip whitespace

                std::string key = line.substr(0, line.find("="));
                std::string value = line.substr(line.find("=") + 1);
                
                if (key == "name")
                    m_name = value;
                else if (key == "link")
                {
                    size_t pos = 0;
                    std::string lib;

                    while ((pos = value.find(",")) != std::string::npos) 
                    {
                        lib = value.substr(0, pos);
                        m_linkPaths.push_back(lib);
                        value.erase(0, pos + 1);
                    }

                    m_linkPaths.push_back(value);
                }
            }

            if (m_name.size() <= 0)
                std::cout << "Project name not set in " << fileName << std::endl;
        }
        else
            std::cout << "Could not open project file: " << fileName << std::endl;
    }
    
    void loadFiles()
    {
        loadIncludeDir("depend");
        loadCompileDir("src");
    }

    void build()
    {
        if (m_compilePaths.size() <= 0)
        {
            std::cout << "No .c or .cpp file found in src directory." << std::endl;
            return;
        }

        std::stringstream outputCommandBuffer;
        outputCommandBuffer << "g++ ";

        for (std::string& flag : m_stringFlags)
            outputCommandBuffer << flag << " ";

        for (std::string& path : m_compilePaths)
            outputCommandBuffer << path << " ";
        
        std::cout << "Binary Search Locations: " << std::endl;

        for (std::string& path : m_binarySearchLocations)
        {
            std::cout << "\t" << path << std::endl;
            outputCommandBuffer << "-L" << path << " ";
        }

        std::cout << "Linking: " << std::endl;

        for (std::string& path : m_linkPaths)
        {
            std::cout << "\t" << path << std::endl;
            outputCommandBuffer << "-l" << path << " ";
        }


        std::cout << "Including: " << std::endl;

        for (std::string& path : m_includePaths)
        {
            std::cout << "\t" << path << std::endl;
            outputCommandBuffer << "-I" << path << " ";
        }
        
        struct stat st = {0};

        if (stat("target", &st) == -1)
            mkdir("target");

        if (m_flags & ProjectFlags::Debug)
        {
            if (stat("target\\debug", &st) == -1)
                mkdir("target\\debug");

            outputCommandBuffer << "-o " << "target\\debug\\" << m_name;
        }
        else
        {
            if (stat("target\\release", &st) == -1)
                mkdir("target\\release");

            outputCommandBuffer << "-o " << "target\\release\\" << m_name;
        }

        std::string outputCommand = outputCommandBuffer.str();

        std::cout << "Generated command: " << outputCommand << std::endl;
        system(outputCommand.c_str());
    }

private:
    void loadIncludeDir(const std::string_view& dirName)
    {
        DIR *dir;
        struct dirent *ent;

        if ((dir = opendir(dirName.data())) != nullptr)
        {
            while ((ent = readdir(dir)) != nullptr) 
            {
                std::string name = ent->d_name;
                std::string path = stringFormat("%s\\%s", dirName.data(), name.c_str());

                if (isFile(path.c_str()) == false)
                {
                    if (name != "." && name != "..")
                    {
                        std::string includeDirectory = stringFormat("%s\\include", path.c_str());
                        m_includePaths.push_back(includeDirectory);

                        std::string binDir = stringFormat("%s\\bin", path.c_str());
                        loadLibDir(binDir);
                    }
                }
            }

            closedir(dir);
        } 
    }

    void loadLibDir(const std::string_view& dirName)
    {
        DIR *dir;
        struct dirent *ent;

        if ((dir = opendir(dirName.data())) != nullptr)
        {
            m_binarySearchLocations.push_back(std::string(dirName));

            while ((ent = readdir(dir)) != nullptr) 
            {
                std::string name = ent->d_name;
                std::string path = stringFormat("%s\\%s", dirName.data(), name.c_str());

                if (isFile(path.c_str()) == true)
                {
                    std::string extension = name.substr(name.find(".") + 1);
                    std::string noExtension = name.substr(0, name.find("."));
                    std::string pathNoExtension = stringFormat("%s\\%s", dirName.data(), noExtension.c_str());

                    if (extension == "lib" || extension == "a")
                    {
                        // std::string fullPath = stringFormat("%s\\%s", getCurrentDirectory().c_str(), path.c_str());
                        m_linkPaths.push_back(noExtension);
                    }
                }

            }

            closedir(dir);
        } 
    }

    void loadCompileDir(const std::string_view& dirName)
    {
        DIR *dir;
        struct dirent *ent;

        if ((dir = opendir(dirName.data())) != nullptr)
        {
            while ((ent = readdir(dir)) != nullptr) 
            {
                std::string name = ent->d_name;
                std::string path = stringFormat("%s/%s", dirName.data(), name.c_str());

                if (isFile(path.c_str()) == true)
                {
                    std::string extension = name.substr(name.find(".") + 1);

                    if (extension == "cpp" || extension == "c")
                        m_compilePaths.push_back(path);
                }
                else
                {
                    if (name != "." && name != "..")
                    {
                        if (name == "include")
                            loadCompileDir(path);
                    }
                }

            }

            closedir(dir);
        } 
    }

private:
    ProjectFlags m_flags;
    std::string m_name;

    std::vector<std::string> m_compilePaths;
    std::vector<std::string> m_linkPaths;
    std::vector<std::string> m_includePaths;
    std::vector<std::string> m_stringFlags;
    std::vector<std::string> m_binarySearchLocations;
};

int main(int argc, char** args)
{
    std::vector<std::string> submitArgs;

    for (size_t argi = 1; argi < argc; argi++)
        submitArgs.push_back(std::string(args[argi]));

    Project proj(submitArgs);
    proj.parse("proj.bild");
    proj.loadFiles();
    proj.build();

    return EXIT_SUCCESS;
}