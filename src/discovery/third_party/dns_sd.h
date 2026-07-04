/* Minimal DNS Service Discovery (Bonjour) declarations.
 *
 * This is a trimmed, self-contained subset of Apple's dns_sd.h, carrying only
 * the functions, callbacks, types and constants Beebium's Bonjour advertiser
 * and browser use. It exists so those files compile on Windows against Apple's
 * Bonjour runtime (dnssd.dll) WITHOUT requiring the Bonjour SDK to be
 * installed. The symbols are resolved at run time via GetProcAddress (see
 * DnssdApi.cpp); the declarations here are only for their types and signatures.
 * On macOS the system <dns_sd.h> is used instead of this file.
 *
 * Signatures are transcribed verbatim from Apple's dns_sd.h (mDNSResponder,
 * BSD-3-Clause). Copyright (c) Apple Inc.; see that header for the full notice.
 */

#ifndef BEEBIUM_VENDORED_DNS_SD_H
#define BEEBIUM_VENDORED_DNS_SD_H

#include <stdint.h>

#if defined(_WIN32)
#include <winsock2.h>
typedef SOCKET dnssd_sock_t;
#define DNSSD_API __stdcall
#else
typedef int dnssd_sock_t;
#define DNSSD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DNSServiceRef_t *DNSServiceRef;
typedef uint32_t DNSServiceFlags;
typedef uint32_t DNSServiceProtocol;
typedef int32_t DNSServiceErrorType;

enum {
    kDNSServiceErr_NoError            = 0
};

enum {
    kDNSServiceFlagsAdd              = 0x2
};

enum {
    kDNSServiceProtocol_IPv4         = 0x01
};

enum {
    kDNSServiceInterfaceIndexAny     = 0
};

typedef void (DNSSD_API *DNSServiceRegisterReply)(
    DNSServiceRef sdRef, DNSServiceFlags flags, DNSServiceErrorType errorCode,
    const char *name, const char *regtype, const char *domain, void *context);

typedef void (DNSSD_API *DNSServiceBrowseReply)(
    DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    DNSServiceErrorType errorCode, const char *serviceName, const char *regtype,
    const char *replyDomain, void *context);

typedef void (DNSSD_API *DNSServiceResolveReply)(
    DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    DNSServiceErrorType errorCode, const char *fullname, const char *hosttarget,
    uint16_t port /* network byte order */, uint16_t txtLen,
    const unsigned char *txtRecord, void *context);

typedef void (DNSSD_API *DNSServiceGetAddrInfoReply)(
    DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    DNSServiceErrorType errorCode, const char *hostname,
    const struct sockaddr *address, uint32_t ttl, void *context);

DNSServiceErrorType DNSSD_API DNSServiceRegister(
    DNSServiceRef *sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    const char *name, const char *regtype, const char *domain,
    const char *host, uint16_t port /* network byte order */, uint16_t txtLen,
    const void *txtRecord, DNSServiceRegisterReply callBack, void *context);

DNSServiceErrorType DNSSD_API DNSServiceBrowse(
    DNSServiceRef *sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    const char *regtype, const char *domain, DNSServiceBrowseReply callBack,
    void *context);

DNSServiceErrorType DNSSD_API DNSServiceResolve(
    DNSServiceRef *sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    const char *name, const char *regtype, const char *domain,
    DNSServiceResolveReply callBack, void *context);

DNSServiceErrorType DNSSD_API DNSServiceGetAddrInfo(
    DNSServiceRef *sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
    DNSServiceProtocol protocol, const char *hostname,
    DNSServiceGetAddrInfoReply callBack, void *context);

dnssd_sock_t DNSSD_API DNSServiceRefSockFD(DNSServiceRef sdRef);
DNSServiceErrorType DNSSD_API DNSServiceProcessResult(DNSServiceRef sdRef);
void DNSSD_API DNSServiceRefDeallocate(DNSServiceRef sdRef);

typedef union _TXTRecordRef_t {
    char PrivateData[16];
    char *ForceNaturalAlignment;
} TXTRecordRef;

void DNSSD_API TXTRecordCreate(
    TXTRecordRef *txtRecord, uint16_t bufferLen, void *buffer);
void DNSSD_API TXTRecordDeallocate(TXTRecordRef *txtRecord);
DNSServiceErrorType DNSSD_API TXTRecordSetValue(
    TXTRecordRef *txtRecord, const char *key, uint8_t valueSize,
    const void *value);
uint16_t DNSSD_API TXTRecordGetLength(const TXTRecordRef *txtRecord);
const void * DNSSD_API TXTRecordGetBytesPtr(const TXTRecordRef *txtRecord);
uint16_t DNSSD_API TXTRecordGetCount(uint16_t txtLen, const void *txtRecord);
DNSServiceErrorType DNSSD_API TXTRecordGetItemAtIndex(
    uint16_t txtLen, const void *txtRecord, uint16_t itemIndex,
    uint16_t keyBufLen, char *key, uint8_t *valueLen, const void **value);

#ifdef __cplusplus
}
#endif

#endif  /* BEEBIUM_VENDORED_DNS_SD_H */
