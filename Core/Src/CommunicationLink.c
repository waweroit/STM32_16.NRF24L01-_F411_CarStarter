#include "CommunicationLink.h"

const char *CommunicationLink_StatusToString(CommunicationLinkStatus_t status)
{
    switch (status)
    {
    case COMMUNICATION_LINK_OK: return "OK";
    case COMMUNICATION_LINK_TIMEOUT: return "TIMEOUT";
    case COMMUNICATION_LINK_MAX_RETRY: return "MAX_RETRY";
    case COMMUNICATION_LINK_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case COMMUNICATION_LINK_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}
