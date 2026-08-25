foreach(requiredVariable IN ITEMS SIGNTOOL ARTIFACT THUMBPRINT TIMESTAMP_URL)
    if(NOT DEFINED ${requiredVariable} OR "${${requiredVariable}}" STREQUAL "")
        message(FATAL_ERROR "Signing requires ${requiredVariable}")
    endif()
endforeach()

if(NOT EXISTS "${SIGNTOOL}")
    message(FATAL_ERROR "signtool was not found: ${SIGNTOOL}")
endif()
if(NOT EXISTS "${ARTIFACT}")
    message(FATAL_ERROR "Artifact to sign was not found: ${ARTIFACT}")
endif()

execute_process(
    COMMAND "${SIGNTOOL}" sign
        /sha1 "${THUMBPRINT}"
        /fd SHA256
        /tr "${TIMESTAMP_URL}"
        /td SHA256
        "${ARTIFACT}"
    RESULT_VARIABLE signResult
)
if(NOT signResult EQUAL 0)
    message(FATAL_ERROR "signtool sign failed for ${ARTIFACT}")
endif()

execute_process(
    COMMAND "${SIGNTOOL}" verify /pa /all "${ARTIFACT}"
    RESULT_VARIABLE verifyResult
)
if(NOT verifyResult EQUAL 0)
    message(FATAL_ERROR "signtool verification failed for ${ARTIFACT}")
endif()
