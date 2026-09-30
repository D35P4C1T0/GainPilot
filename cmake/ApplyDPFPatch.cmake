# Patch a private source snapshot; never mutate the supplied DPF checkout.
function(gainpilot_prepare_dpf_copy)
  set(copy_root "${CMAKE_CURRENT_BINARY_DIR}/gainpilot-dpf-source")
  # Serialize configurations of the same build tree. Separate trees are independent.
  file(LOCK "${CMAKE_CURRENT_BINARY_DIR}/gainpilot-dpf-copy.lock" GUARD FUNCTION TIMEOUT 120)
  file(REMOVE_RECURSE "${copy_root}")
  # Copy working-tree contents, including intentional edits and nested dependency
  # sources. Exclude all repository metadata, including submodule .git pointer files.
  file(COPY "${GAINPILOT_DPF_PATH}/" DESTINATION "${copy_root}"
    PATTERN ".git" EXCLUDE)
  # Anchor git apply in this snapshot even when the build is inside a worktree.
  execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet
    WORKING_DIRECTORY "${copy_root}" COMMAND_ERROR_IS_FATAL ANY)
  foreach(patch_name IN LISTS ARGN)
    set(patch_path "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/${patch_name}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --check "${patch_path}"
      WORKING_DIRECTORY "${copy_root}" RESULT_VARIABLE can_apply OUTPUT_QUIET ERROR_QUIET)
    if(can_apply EQUAL 0)
      execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero "${patch_path}"
        WORKING_DIRECTORY "${copy_root}" COMMAND_ERROR_IS_FATAL ANY)
    else()
      execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --reverse --check "${patch_path}"
        WORKING_DIRECTORY "${copy_root}" RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
      if(NOT already_applied EQUAL 0)
        message(FATAL_ERROR "GainPilot DPF patch ${patch_name} conflicts with ${GAINPILOT_DPF_PATH}; source edits were preserved.")
      endif()
    endif()
  endforeach()
  set(GAINPILOT_EFFECTIVE_DPF_PATH "${copy_root}" PARENT_SCOPE)
endfunction()
