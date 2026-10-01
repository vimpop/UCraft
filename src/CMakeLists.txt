add_executable(${PROJECT_NAME} main.c UCraft.c socketio.c player.c c2s.c s2c.c util.c log.c  encryption.c https.c game.c world.c blocks.c storage.c)
target_link_libraries(${PROJECT_NAME} PRIVATE ucraft_options)


if(UNIX)
    #run the size command afterwards
    if(APPLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND size ${PROJECT_NAME})
    else()
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND size --format=berkeley ${PROJECT_NAME})
    endif()
endif()
