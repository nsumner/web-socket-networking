# Apply private compile/link options for a build target.
function(networking_apply_options target)
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
  )
  if(NETWORKING_NO_RTTI)
    target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-rtti>
      $<$<CXX_COMPILER_ID:MSVC>:/GR->
    )
  endif()
  if(NETWORKING_ENABLE_SANITIZERS)
    # Only supported for GCC/Clang.
    target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    target_link_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined>)
  endif()
endfunction()
