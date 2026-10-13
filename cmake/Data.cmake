function(vw_copy_data target)
    add_custom_target(${target}_data
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${PROJECT_SOURCE_DIR}/data $<TARGET_FILE_DIR:${target}>/data)
    add_dependencies(${target} ${target}_data)
endfunction()
