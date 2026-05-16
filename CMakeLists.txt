# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

idf_component_register(
  INCLUDE_DIRS "src"
  SRCS "src/vid6608.cpp"
  REQUIRES driver esp_driver_rmt
  PRIV_REQUIRES esp_driver_gpio
)
