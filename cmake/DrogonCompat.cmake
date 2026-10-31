# Drogon's exported interface names jsoncpp by a target that vcpkg's jsoncpp config does not define,
# so any target linking Drogon has to align the two names first. Drogon is an overlay port, so this
# belongs here rather than in each component that happens to link it.
function(cms_learn_align_jsoncpp)
  if(NOT TARGET jsoncpp_lib AND TARGET jsoncpp_static)
    add_library(jsoncpp_lib ALIAS jsoncpp_static)
  endif()
endfunction()
