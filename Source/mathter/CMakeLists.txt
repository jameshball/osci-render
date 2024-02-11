add_library(Mathter INTERFACE)

message("${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(Mathter INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_sources(Mathter
	INTERFACE
		"Mathter.natvis"
)

target_compile_features(Mathter INTERFACE cxx_std_17)

if (${MATHTER_USE_XSIMD})
	find_package(xsimd)
	target_link_libraries(Mathter INTERFACE xsimd)
endif()