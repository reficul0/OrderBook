cmake_minimum_required(VERSION 3.0.0)
project( OrderBookTests )

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)
file(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# Формируем цель
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

find_package( 
	Boost 1.64 REQUIRED 
	COMPONENTS unit_test_framework filesystem system date_time chrono thread locale iostreams
)

target_include_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE  ${Boost_LIBRARY_DIR_DEBUG})

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp PROPERTIES COTIRE_EXCLUDED ON)

if(UNIX)
	set(OrderBookLibPath "OrderBook.a")
elseif(WIN32)
	set(OrderBookLibPath "${CMAKE_BUILD_TYPE}/OrderBook.lib")
endif()

set(OrderBookLibDir ${CMAKE_BINARY_DIR}/lib/)
add_library(OrderBookLib STATIC IMPORTED)
set_target_properties(OrderBookLib
	PROPERTIES 
		IMPORTED_LOCATION "${OrderBookLibDir}${OrderBookLibPath}"
		INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/OrderBook
)


target_link_libraries(${PROJECT_NAME} OrderBookLib boost_unit_test_framework boost_system boost_filesystem boost_chrono boost_thread boost_locale boost_date_time boost_timer boost_iostreams)
if(UNIX)
	find_library(pthread REQUIRED)
	target_link_libraries(${PROJECT_NAME} pthread)
endif()


target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)

add_dependencies(${PROJECT_NAME} OrderBook)
