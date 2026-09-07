#include <cxxopts.hpp>
#include <iostream>
#include <string>
#include <filesystem>

#include "core/scanner.hpp"

using namespace project_describer;

int main(int argc, char* argv[])
{
    try
    {
        // 1. 解析命令行参数
        cxxopts::Options options(
            "project-describer",
            "A tool to describe and analyze project structures."
        );

        options.add_options()
            ("p,path", "Target project root path", cxxopts::value<std::string>())
            ("h,help", "Print this help message");

        auto result = options.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (!result.count("path"))
        {
            std::cerr << "Error: The -p/--path parameter is required." << std::endl;
            std::cerr << "Use --help for usage information." << std::endl;
            return 1;
        }

        std::string project_path_str = result["path"].as<std::string>();
        std::filesystem::path project_path(project_path_str);

        std::cout << "Project root: " << project_path_str << std::endl;
        std::cout << "Scanning..." << std::endl;

        // 2. 扫描目录
        auto scan_result = Scanner::scan(project_path, [](const std::string& path)
            {
                // 进度输出（可选），每 100 个文件输出一次
                static int counter = 0;
                if (++counter % 100 == 0)
                {
                    std::cout << "  Scanned " << counter << " files..." << std::endl;
                }
            });

        // 3. 输出结果
        std::cout << "\n=== Scan Summary ===" << std::endl;
        std::cout << "Total files scanned: " << scan_result.total_files << std::endl;
        std::cout << "Text files found: " << scan_result.text_files << std::endl;

        if (!scan_result.errors.empty())
        {
            std::cout << "\n=== Errors ===" << std::endl;
            for (const auto& err : scan_result.errors)
            {
                std::cerr << "  " << err << std::endl;
            }
        }

        std::cout << "\n=== Text Files ===" << std::endl;
        for (const auto& item : scan_result.files)
        {
            std::cout << "  [" << item.relative_path << "]" << std::endl;
            // 内容预览（前 200 字符）
            std::string preview = item.content;
            if (preview.size() > 200)
            {
                preview = preview.substr(0, 200) + "...";
            }
            // 替换换行符以便单行显示
            std::replace(preview.begin(), preview.end(), '\n', ' ');
            std::cout << "    " << preview << std::endl;
            std::cout << std::endl;
        }

        std::cout << "Done." << std::endl;
        return 0;

    }
    catch (const cxxopts::exceptions::exception& e)
    {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return 1;
    }
}