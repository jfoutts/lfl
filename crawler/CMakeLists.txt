# $Id: CMakeLists.txt 1320 2014-10-26 08:12:52Z justin $

set(LFL_HEADLESS 1)
set(LFL_PROTOBUF 1)

if(LFL_ANDROID)
elseif(LFL_IPHONE)
else(LFL_ANDROID)
 set(LFL_GLOG 1)
endif(LFL_ANDROID)

lfl_project(crawler)
add_subdirectory(../lfapp ${CMAKE_CURRENT_BINARY_DIR}/lfapp_obj)
add_definitions(${LFAPP_DEF})
include_directories(${LFAPP_INCLUDE})

PROTOBUF_GENERATE_CPP(PROTO_SRC PROTO_HDR crawler.proto)

# crawler
add_executable(crawler crawler.cpp ${SDL_MAIN} ${PROTO_SRC})
target_link_libraries(crawler ${LFAPP_LIB})
lfapp_post_build(crawler crawler)

