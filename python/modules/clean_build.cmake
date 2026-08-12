file(REMOVE_RECURSE
    "${PY_MODULE_DIR}/build"
    "${PY_MODULE_DIR}/dist"
    "${PY_MODULE_DIR}/libkperf.egg-info"
)

file(REMOVE
    "${PY_MODULE_DIR}/_libkperf/libkperf.so"
    "${PY_MODULE_DIR}/_libkperf/libsym.so"
)