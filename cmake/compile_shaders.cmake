# cmake/compile_shaders.cmake
include_guard(GLOBAL)

# Allow override: -DSLANGC=/path/to/slangc
set(SLANGC "" CACHE FILEPATH "Path to slangc executable")
if (SLANGC STREQUAL "")
  find_program(SLANGC slangc)
endif()

if (SLANGC STREQUAL "" OR NOT SLANGC)
  message(FATAL_ERROR
    "SLANGC is not set and slangc was not found in PATH. "
    "Set -DSLANGC=/path/to/slangc or ensure slangc is in PATH."
  )
endif()

message(STATUS "SLANGC = ${SLANGC}")

function(vk_compile_slang_shaders OUT_SPV_LIST TARGET_NAME SHADER_DIR SPIRV_DIR)
  file(MAKE_DIRECTORY "${SPIRV_DIR}")

  file(GLOB_RECURSE _SLANG_SHADERS
    CONFIGURE_DEPENDS
    "${SHADER_DIR}/*.slang"
  )

  set(_OUTPUTS)

  foreach(_SHADER IN LISTS _SLANG_SHADERS)
    file(RELATIVE_PATH _REL_PATH "${SHADER_DIR}" "${_SHADER}")
    string(REPLACE ".slang" ".spv" _SPV_REL_PATH "${_REL_PATH}")
    set(_OUTPUT_FILE "${SPIRV_DIR}/${_SPV_REL_PATH}")

    get_filename_component(_OUTPUT_DIR "${_OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_OUTPUT_DIR}")

    if (_SHADER MATCHES "\\.vert\\.slang$")
      set(_STAGE vertex)
    elseif (_SHADER MATCHES "\\.frag\\.slang$")
      set(_STAGE fragment)
    elseif (_SHADER MATCHES "\\.comp\\.slang$")
      set(_STAGE compute)
    else()
      message(FATAL_ERROR "Unknown shader stage for ${_SHADER} (expected .vert/.frag/.comp)")
    endif()

    add_custom_command(
      OUTPUT "${_OUTPUT_FILE}"
      COMMAND "${SLANGC}"
              -target spirv
              -profile glsl_460
              -entry main
              -stage ${_STAGE}
              "${_SHADER}"
              -o "${_OUTPUT_FILE}"
      DEPENDS "${_SHADER}"
      COMMENT "slangc ${_REL_PATH} -> ${_SPV_REL_PATH}"
      VERBATIM
    )

    list(APPEND _OUTPUTS "${_OUTPUT_FILE}")
  endforeach()

  # Only create the target ONCE
  if (TARGET "${TARGET_NAME}")
    message(FATAL_ERROR "Target '${TARGET_NAME}' already exists. Choose a different name.")
  endif()

  add_custom_target("${TARGET_NAME}" DEPENDS ${_OUTPUTS})

  set("${OUT_SPV_LIST}" "${_OUTPUTS}" PARENT_SCOPE)
endfunction()