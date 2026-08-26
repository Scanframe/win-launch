# Set the target name using the project name as a base name.
set(TEST_TARGET "${PROJECT_NAME}-test")

add_executable("${TEST_TARGET}")
target_sources("${TEST_TARGET}"
	PRIVATE
	test-main.c
	../main.h
	../main.c
)
# Prevent main to be included.
target_compile_definitions(${TEST_TARGET} PRIVATE SF_NO_MAIN)
# Add to the ctest application.
sf_add_test("${TEST_TARGET}" "Plain")
