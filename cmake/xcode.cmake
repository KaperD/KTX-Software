# Macro for setting up code signing on targets
macro (set_xcode_code_sign target)
set_target_properties(${target} PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${XCODE_CODE_SIGN_IDENTITY}"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${XCODE_DEVELOPMENT_TEAM}"
    XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--timestamp"
    XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS "NO"
    BUILD_WITH_INSTALL_RPATH ON
)
endmacro (set_xcode_code_sign)
