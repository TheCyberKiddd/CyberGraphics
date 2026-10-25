# Runnable samples. Every sample compiles in CI; a sample that needs a window
# still builds on platforms whose backend returns Unsupported at runtime.

add_subdirectory(00_window)
add_subdirectory(01_gradient)
add_subdirectory(02_raster)
add_subdirectory(03_depth)
add_subdirectory(04_textured)
