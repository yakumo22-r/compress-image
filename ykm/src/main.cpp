#include "app_permissions.h"
#include "desktop_host.h"
#include "session.h"
#include "sys_api.h"
#include "ui_imgui_api.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <ykm/embedded_app.h>

namespace {

void install_default_api_units(ykm::SessionManager& manager) {
    manager.add_api_unit(ykm::sys_api_unit());
    manager.add_api_unit(ykm::ui_imgui_api_unit());
}

void print_usage() {
    std::cout << "Usage:\n"
              << "  ykm [widget-root]\n"
              << "  ykm --cli <script.js> [args...]\n"
              << "  ykm --embedded-cli [args...]\n";
}

int run_embedded_cli(int argc, char** argv) {
    ykm::load_embedded_permissions();
    const YkmEmbeddedApp* app = ykm_get_embedded_app();
    if (!app || app->struct_size < sizeof(YkmEmbeddedApp)) {
        std::cerr << "No embedded application\n";
        return 2;
    }
    const YkmEmbeddedSession* cli = nullptr;
    for (uint32_t index = 0; index < app->session_count; ++index) {
        if (std::string(app->sessions[index].role) == "cli") {
            cli = &app->sessions[index];
            break;
        }
    }
    if (!cli) {
        std::cerr << "Embedded application has no CLI session\n";
        return 2;
    }
    std::vector<std::string> arguments;
    for (int index = 2; index < argc; ++index) arguments.emplace_back(argv[index]);
    ykm::SessionManager manager;
    install_default_api_units(manager);
    ykm::Session session(manager, cli->name);
    session.set_arguments(arguments);
    if (!session.load_embedded(cli->module, false)) {
        std::cerr << session.error() << '\n';
        return 1;
    }
    return session.run_cli(arguments);
}

int run_cli(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 2;
    }
    std::vector<std::string> arguments;
    for (int index = 3; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    ykm::SessionManager manager;
    install_default_api_units(manager);
    std::filesystem::path script = std::filesystem::absolute(argv[2]);
    std::filesystem::path root = script.parent_path().empty() ? std::filesystem::current_path() : script.parent_path();
    if (root.filename() == "sessions" && std::filesystem::exists(root.parent_path() / "ykm.manifest.json")) {
        root = root.parent_path();
    }
    manager.set_root(root);
    ykm::Session session(manager, "cli");
    session.set_arguments(arguments);
    if (!session.load(script.string(), false)) {
        std::cerr << session.error() << '\n';
        return 1;
    }
    return session.run_cli(arguments);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--") {
        --argc;
        ++argv;
    }
    if (argc > 1 && std::string(argv[1]) == "--cli") {
        return run_cli(argc, argv);
    }
    if (argc > 1 && std::string(argv[1]) == "--embedded-cli") {
        return run_embedded_cli(argc, argv);
    }
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage();
        return 0;
    }
    return run_desktop_host(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("widgets"));
}
