# =============================================================================
#  Proyecto: maquina_expendedora
#  Plataforma: ESP-IDF (ESP32-32E 4.0")
#  Descripcion: CMake raiz del proyecto. El punto de entrada del firmware se
#               encuentra en main/main.c (app_main).
# =============================================================================

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(maquina_expendedora)
