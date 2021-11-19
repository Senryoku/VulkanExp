#include "Application.hpp"

void Application::drawUI() {
    if(ImGui::BeginMainMenuBar()) {
        if(ImGui::BeginMenu("File")) {
            if(ImGui::MenuItem("Load Scene")) {
            }
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Debug")) {
            if(ImGui::MenuItem("Compile Shaders")) {
                compileShaders();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    if(ImGui::Begin("Logs?")) {
        ImGui::End();
    }
    ImGui::ShowDemoWindow();
}