project(mattausch)

cmake_minimum_required(VERSION 3.2.0)

# C++14 support
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp -std=c++14 -fpermissive")

# Sources
set (mattausch_SRC
        "src/main.cpp"
        "src/mesh.h"
        "src/scene.cpp"
        "src/objloader.h"
        "src/ray.cpp"
        "src/rfimage.h"
        "src/psf.h"
        "src/transducer.h"
        "src/volume.h"
        "src/wavefront/tiny_obj_loader.cpp")

add_executable(mattausch ${mattausch_SRC})

# Bullet includes
set(BULLET_SRC_DIR "" CACHE PATH "")
set(BULLET_LIB_DIR "" CACHE PATH "")
message("Bullet source directory: ${BULLET_SRC_DIR}")
message("Bullet lib directory: ${BULLET_LIB_DIR}")
include_directories(${BULLET_SRC_DIR})

target_link_libraries(mattausch
    ${BULLET_LIB_DIR}/libBullet3Common.a
    ${BULLET_LIB_DIR}/libBulletDynamics.a
    ${BULLET_LIB_DIR}/libBulletCollision.a
    ${BULLET_LIB_DIR}/libLinearMath.a
    )

# OpenCV
find_package(OpenCV REQUIRED)
if (OpenCV_FOUND)
  target_link_libraries(mattausch ${OpenCV_LIBS})
endif()

# Include other stuff, like Units
include_directories("${CMAKE_SOURCE_DIR}/include")
