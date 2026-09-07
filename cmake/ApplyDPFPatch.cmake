# Apply project-owned compatibility fixes without replacing other DPF edits.
function(gainpilot_apply_dpf_patch patch_name)
  set(patch_path "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/${patch_name}")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --check "${patch_path}"
    WORKING_DIRECTORY "${GAINPILOT_DPF_PATH}"
    RESULT_VARIABLE can_apply OUTPUT_QUIET ERROR_QUIET
  )
  if(can_apply EQUAL 0)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero "${patch_path}"
      WORKING_DIRECTORY "${GAINPILOT_DPF_PATH}"
      COMMAND_ERROR_IS_FATAL ANY
    )
  else()
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --reverse --check "${patch_path}"
      WORKING_DIRECTORY "${GAINPILOT_DPF_PATH}"
      RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT already_applied EQUAL 0)
      message(FATAL_ERROR
        "The GainPilot DPF patch ${patch_name} does not apply cleanly to ${GAINPILOT_DPF_PATH}.")
    endif()
  endif()
endfunction()
