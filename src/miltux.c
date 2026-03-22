/*
 * MiLTuX - Error message helper
 */

#include "miltux.h"

const char *miltux_strerror(miltux_err_t err)
{
    switch (err) {
    case MILTUX_OK:           return "success";
    case MILTUX_ERR_PERM:     return "permission denied";
    case MILTUX_ERR_NOENT:    return "no such file or directory";
    case MILTUX_ERR_EXIST:    return "file already exists";
    case MILTUX_ERR_INVAL:    return "invalid argument";
    case MILTUX_ERR_NOMEM:    return "out of memory";
    case MILTUX_ERR_NOTDIR:   return "not a directory";
    case MILTUX_ERR_ISDIR:    return "is a directory";
    case MILTUX_ERR_NOTEMPTY: return "directory not empty";
    case MILTUX_ERR_RANGE:    return "value out of range";
    case MILTUX_ERR_NET:      return "network error";
    default:                  return "unknown error";
    }
}
