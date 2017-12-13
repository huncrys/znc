/*
 * Copyright (C) 2004-2017 ZNC, see the NOTICE file for details.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef __CYGWIN__
#ifndef _XOPEN_SOURCE
// strptime() wants this
#define _XOPEN_SOURCE 600
#endif
#endif

#include <znc/Utils.h>
#include <znc/ZNCDebug.h>
#include <znc/FileUtils.h>
#include <znc/Message.h>
#ifdef HAVE_LIBSSL
#include <openssl/ssl.h>
#include <memory>
#endif /* HAVE_LIBSSL */
#include <unistd.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#ifdef HAVE_TCSETATTR
#include <termios.h>
#endif

#ifdef HAVE_ICU
#include <unicode/ucnv.h>
#include <unicode/errorcode.h>
#endif

// Required with GCC 4.3+ if openssl is disabled
#include <cstring>
#include <cstdlib>
#include <iomanip>

using std::map;
using std::vector;

CUtils::CUtils() {}
CUtils::~CUtils() {}

#ifdef HAVE_LIBSSL
// Generated by "openssl dhparam 2048"
constexpr const char* szDefaultDH2048 =
    "-----BEGIN DH PARAMETERS-----\n"
    "MIIBCAKCAQEAtS/K3TMY8IHzcCATQSjUF3rDidjDDQmT+mLxyxRORmzMPjFIFkKH\n"
    "MOmxZvyCBArdaoCCEBBOzrldl/bBLn5TOeZb+MW7mpBLANTuQSOu97DDM7EzbnqC\n"
    "b6z3QgixZ2+UqxdmQAu4nBPLFwym6W/XPFEHpz6iHISSvjzzo4cfI0xwWTcoAvFQ\n"
    "r/ZU5BXSXp7XuDxSyyAqaaKUxquElf+x56QWrpNJypjzPpslg5ViAKwWQS0TnCrU\n"
    "sVuhFtbNlZjqW1tMSBxiWFltS1HoEaaI79MEpf1Ps25OrQl8xqqCGKkZcHlNo4oF\n"
    "cvUyzAEcCQYHmiYjp2hoZbSa8b690TQaAwIBAg==\n"
    "-----END DH PARAMETERS-----\n";

void CUtils::GenerateCert(FILE* pOut, const CString& sHost) {
    const int days = 365;
    const int years = 10;

    unsigned int uSeed = (unsigned int)time(nullptr);
    int serial = (rand_r(&uSeed) % 9999);

    std::unique_ptr<BIGNUM, void (*)(BIGNUM*)> pExponent(BN_new(), ::BN_free);
    if (!pExponent || !BN_set_word(pExponent.get(), 0x10001)) return;

    std::unique_ptr<RSA, void (*)(RSA*)> pRSA(RSA_new(), ::RSA_free);
    if (!pRSA ||
        !RSA_generate_key_ex(pRSA.get(), 2048, pExponent.get(), nullptr))
        return;

    std::unique_ptr<EVP_PKEY, void (*)(EVP_PKEY*)> pKey(EVP_PKEY_new(),
                                                        ::EVP_PKEY_free);
    if (!pKey || !EVP_PKEY_set1_RSA(pKey.get(), pRSA.get())) return;

    std::unique_ptr<X509, void (*)(X509*)> pCert(X509_new(), ::X509_free);
    if (!pCert) return;

    X509_set_version(pCert.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(pCert.get()), serial);
    X509_gmtime_adj(X509_get_notBefore(pCert.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(pCert.get()),
                    (long)60 * 60 * 24 * days * years);
    X509_set_pubkey(pCert.get(), pKey.get());

    const char* pLogName = getenv("LOGNAME");
    const char* pHostName = nullptr;

    if (!pLogName) pLogName = "Unknown";

    if (!sHost.empty()) pHostName = sHost.c_str();

    if (!pHostName) pHostName = getenv("HOSTNAME");

    if (!pHostName) pHostName = "host.unknown";

    CString sEmailAddr = pLogName;
    sEmailAddr += "@";
    sEmailAddr += pHostName;

    X509_NAME* pName = X509_get_subject_name(pCert.get());
    X509_NAME_add_entry_by_txt(pName, "OU", MBSTRING_ASC,
                               (unsigned char*)pLogName, -1, -1, 0);
    X509_NAME_add_entry_by_txt(pName, "CN", MBSTRING_ASC,
                               (unsigned char*)pHostName, -1, -1, 0);
    X509_NAME_add_entry_by_txt(pName, "emailAddress", MBSTRING_ASC,
                               (unsigned char*)sEmailAddr.c_str(), -1, -1, 0);

    X509_set_issuer_name(pCert.get(), pName);

    if (!X509_sign(pCert.get(), pKey.get(), EVP_sha256())) return;

    PEM_write_RSAPrivateKey(pOut, pRSA.get(), nullptr, nullptr, 0, nullptr,
                            nullptr);
    PEM_write_X509(pOut, pCert.get());

    fprintf(pOut, "%s", szDefaultDH2048);
}
#endif /* HAVE_LIBSSL */

CString CUtils::GetIP(unsigned long addr) {
    char szBuf[16];
    memset((char*)szBuf, 0, 16);

    if (addr >= (1 << 24)) {
        unsigned long ip[4];
        ip[0] = addr >> 24 & 255;
        ip[1] = addr >> 16 & 255;
        ip[2] = addr >> 8 & 255;
        ip[3] = addr & 255;
        sprintf(szBuf, "%lu.%lu.%lu.%lu", ip[0], ip[1], ip[2], ip[3]);
    }

    return szBuf;
}

unsigned long CUtils::GetLongIP(const CString& sIP) {
    unsigned long ret;
    char ip[4][4];
    unsigned int i;

    i = sscanf(sIP.c_str(), "%3[0-9].%3[0-9].%3[0-9].%3[0-9]", ip[0], ip[1],
               ip[2], ip[3]);
    if (i != 4) return 0;

    // Beware that atoi("200") << 24 would overflow and turn negative!
    ret = atol(ip[0]) << 24;
    ret += atol(ip[1]) << 16;
    ret += atol(ip[2]) << 8;
    ret += atol(ip[3]) << 0;

    return ret;
}

// If you change this here and in GetSaltedHashPass(),
// don't forget CUser::HASH_DEFAULT!
// TODO refactor this
const CString CUtils::sDefaultHash = "sha256";
CString CUtils::GetSaltedHashPass(CString& sSalt) {
    sSalt = GetSalt();

    while (true) {
        CString pass1;
        do {
            pass1 = CUtils::GetPass("Enter password");
        } while (pass1.empty());

        CString pass2 = CUtils::GetPass("Confirm password");

        if (!pass1.Equals(pass2, CString::CaseSensitive)) {
            CUtils::PrintError("The supplied passwords did not match");
        } else {
            // Construct the salted pass
            return SaltedSHA256Hash(pass1, sSalt);
        }
    }
}

CString CUtils::GetSalt() { return CString::RandomString(20); }

CString CUtils::SaltedMD5Hash(const CString& sPass, const CString& sSalt) {
    return CString(sPass + sSalt).MD5();
}

CString CUtils::SaltedSHA256Hash(const CString& sPass, const CString& sSalt) {
    return CString(sPass + sSalt).SHA256();
}

CString CUtils::GetPass(const CString& sPrompt) {
#ifdef HAVE_TCSETATTR
    // Disable echo
    struct termios t;
    tcgetattr(1, &t);
    struct termios t2 = t;
    t2.c_lflag &= ~ECHO;
    tcsetattr(1, TCSANOW, &t2);
    // Read pass
    CString r;
    GetInput(sPrompt, r);
    // Restore echo and go to new line
    tcsetattr(1, TCSANOW, &t);
    fprintf(stdout, "\n");
    fflush(stdout);
    return r;
#else
    PrintPrompt(sPrompt);
#ifdef HAVE_GETPASSPHRASE
    return getpassphrase("");
#else
    return getpass("");
#endif
#endif
}

bool CUtils::GetBoolInput(const CString& sPrompt, bool bDefault) {
    return CUtils::GetBoolInput(sPrompt, &bDefault);
}

bool CUtils::GetBoolInput(const CString& sPrompt, bool* pbDefault) {
    CString sRet, sDefault;

    if (pbDefault) {
        sDefault = (*pbDefault) ? "yes" : "no";
    }

    while (true) {
        GetInput(sPrompt, sRet, sDefault, "yes/no");

        if (sRet.Equals("y") || sRet.Equals("yes")) {
            return true;
        } else if (sRet.Equals("n") || sRet.Equals("no")) {
            return false;
        }
    }
}

bool CUtils::GetNumInput(const CString& sPrompt, unsigned int& uRet,
                         unsigned int uMin, unsigned int uMax,
                         unsigned int uDefault) {
    if (uMin > uMax) {
        return false;
    }

    CString sDefault = (uDefault != (unsigned int)~0) ? CString(uDefault) : "";
    CString sNum, sHint;

    if (uMax != (unsigned int)~0) {
        sHint = CString(uMin) + " to " + CString(uMax);
    } else if (uMin > 0) {
        sHint = CString(uMin) + " and up";
    }

    while (true) {
        GetInput(sPrompt, sNum, sDefault, sHint);
        if (sNum.empty()) {
            return false;
        }

        uRet = sNum.ToUInt();

        if ((uRet >= uMin && uRet <= uMax)) {
            break;
        }

        CUtils::PrintError("Number must be " + sHint);
    }

    return true;
}

bool CUtils::GetInput(const CString& sPrompt, CString& sRet,
                      const CString& sDefault, const CString& sHint) {
    CString sExtra;
    CString sInput;
    sExtra += (!sHint.empty()) ? (" (" + sHint + ")") : "";
    sExtra += (!sDefault.empty()) ? (" [" + sDefault + "]") : "";

    PrintPrompt(sPrompt + sExtra);
    char szBuf[1024];
    memset(szBuf, 0, 1024);
    if (fgets(szBuf, 1024, stdin) == nullptr) {
        // Reading failed (Error? EOF?)
        PrintError("Error while reading from stdin. Exiting...");
        exit(-1);
    }
    sInput = szBuf;

    sInput.TrimSuffix("\n");

    if (sInput.empty()) {
        sRet = sDefault;
    } else {
        sRet = sInput;
    }

    return !sRet.empty();
}

#define BOLD "\033[1m"
#define NORM "\033[22m"

#define RED "\033[31m"
#define GRN "\033[32m"
#define YEL "\033[33m"
#define BLU "\033[34m"
#define DFL "\033[39m"

void CUtils::PrintError(const CString& sMessage) {
    if (CDebug::StdoutIsTTY())
        fprintf(stdout, BOLD BLU "[" RED " ** " BLU "]" DFL NORM " %s\n",
                sMessage.c_str());
    else
        fprintf(stdout, "%s\n", sMessage.c_str());
    fflush(stdout);
}

void CUtils::PrintPrompt(const CString& sMessage) {
    if (CDebug::StdoutIsTTY())
        fprintf(stdout, BOLD BLU "[" YEL " ?? " BLU "]" DFL NORM " %s: ",
                sMessage.c_str());
    else
        fprintf(stdout, "[ ?? ] %s: ", sMessage.c_str());
    fflush(stdout);
}

void CUtils::PrintMessage(const CString& sMessage, bool bStrong) {
    if (CDebug::StdoutIsTTY()) {
        if (bStrong)
            fprintf(stdout,
                    BOLD BLU "[" YEL " ** " BLU "]" DFL BOLD " %s" NORM "\n",
                    sMessage.c_str());
        else
            fprintf(stdout, BOLD BLU "[" YEL " ** " BLU "]" DFL NORM " %s\n",
                    sMessage.c_str());
    } else
        fprintf(stdout, "%s\n", sMessage.c_str());

    fflush(stdout);
}

void CUtils::PrintAction(const CString& sMessage) {
    if (CDebug::StdoutIsTTY())
        fprintf(stdout, BOLD BLU "[ .. " BLU "]" DFL NORM " %s...\n",
                sMessage.c_str());
    else
        fprintf(stdout, "%s... ", sMessage.c_str());
    fflush(stdout);
}

void CUtils::PrintStatus(bool bSuccess, const CString& sMessage) {
    if (CDebug::StdoutIsTTY()) {
        if (bSuccess) {
            if (!sMessage.empty())
                fprintf(stdout,
                        BOLD BLU "[" GRN " >> " BLU "]" DFL NORM " %s\n",
                        sMessage.c_str());
        } else {
            fprintf(stdout, BOLD BLU "[" RED " !! " BLU "]" DFL NORM BOLD RED
                                     " %s" DFL NORM "\n",
                    sMessage.empty() ? "failed" : sMessage.c_str());
        }
    } else {
        if (bSuccess) {
            fprintf(stdout, "%s\n", sMessage.c_str());
        } else {
            if (!sMessage.empty()) {
                fprintf(stdout, "[ %s ]", sMessage.c_str());
            }

            fprintf(stdout, "\n");
        }
    }

    fflush(stdout);
}

namespace {
/* Switch GMT-X and GMT+X
 *
 * See https://en.wikipedia.org/wiki/Tz_database#Area
 *
 * "In order to conform with the POSIX style, those zone names beginning
 * with "Etc/GMT" have their sign reversed from what most people expect.
 * In this style, zones west of GMT have a positive sign and those east
 * have a negative sign in their name (e.g "Etc/GMT-14" is 14 hours
 * ahead/east of GMT.)"
 */
inline CString FixGMT(CString sTZ) {
    if (sTZ.length() >= 4 && sTZ.StartsWith("GMT")) {
        if (sTZ[3] == '+') {
            sTZ[3] = '-';
        } else if (sTZ[3] == '-') {
            sTZ[3] = '+';
        }
    }
    return sTZ;
}
}  // namespace

timeval CUtils::GetTime() {
#ifdef HAVE_CLOCK_GETTIME
    timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return { ts.tv_sec, static_cast<suseconds_t>(ts.tv_nsec / 1000) };
    }
#endif

    struct timeval tv;
    if (gettimeofday(&tv, nullptr) == 0) {
        return tv;
    }

    // Last resort, no microseconds
    return { time(nullptr), 0 };
}

unsigned long long CUtils::GetMillTime() {
    struct timeval tv = GetTime();
    unsigned long long iTime = 0;
    iTime = (unsigned long long)tv.tv_sec * 1000;
    iTime += ((unsigned long long)tv.tv_usec / 1000);
    return iTime;
}

CString CUtils::CTime(time_t t, const CString& sTimezone) {
    char s[30] = {};  // should have at least 26 bytes
    if (sTimezone.empty()) {
        ctime_r(&t, s);
        // ctime() adds a trailing newline
        return CString(s).Trim_n();
    }
    CString sTZ = FixGMT(sTimezone);

    // backup old value
    char* oldTZ = getenv("TZ");
    if (oldTZ) oldTZ = strdup(oldTZ);
    setenv("TZ", sTZ.c_str(), 1);
    tzset();

    ctime_r(&t, s);

    // restore old value
    if (oldTZ) {
        setenv("TZ", oldTZ, 1);
        free(oldTZ);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return CString(s).Trim_n();
}

CString CUtils::FormatTime(time_t t, const CString& sFormat,
                           const CString& sTimezone) {
    char s[1024] = {};
    tm m;
    if (sTimezone.empty()) {
        localtime_r(&t, &m);
        strftime(s, sizeof(s), sFormat.c_str(), &m);
        return s;
    }
    CString sTZ = FixGMT(sTimezone);

    // backup old value
    char* oldTZ = getenv("TZ");
    if (oldTZ) oldTZ = strdup(oldTZ);
    setenv("TZ", sTZ.c_str(), 1);
    tzset();

    localtime_r(&t, &m);
    strftime(s, sizeof(s), sFormat.c_str(), &m);

    // restore old value
    if (oldTZ) {
        setenv("TZ", oldTZ, 1);
        free(oldTZ);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return s;
}

CString CUtils::FormatTime(const timeval& tv, const CString& sFormat,
                           const CString& sTimezone) {
    // Parse additional format specifiers before passing them to
    // strftime, since the way strftime treats unknown format
    // specifiers is undefined.
    CString sFormat2;

    // Make sure %% is parsed correctly, i.e. %%f is passed through to
    // strftime to become %f, and not 123.
    bool bInFormat = false;
    int iDigits;
    CString::size_type uLastCopied = 0, uFormatStart;

    for (CString::size_type i = 0; i < sFormat.length(); i++) {
        if (!bInFormat) {
            if (sFormat[i] == '%') {
                uFormatStart = i;
                bInFormat = true;
                iDigits = 3;
            }
        } else {
            switch (sFormat[i]) {
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    iDigits = sFormat[i] - '0';
                    break;
                case 'f': {
                    int iVal = tv.tv_usec;
                    int iDigitDelta = iDigits - 6; // tv_user is in 10^-6 seconds
                    for (; iDigitDelta > 0; iDigitDelta--)
                        iVal *= 10;
                    for (; iDigitDelta < 0; iDigitDelta++)
                        iVal /= 10;
                    sFormat2 += sFormat.substr(uLastCopied,
                        uFormatStart - uLastCopied);
                    CString sVal = CString(iVal);
                    sFormat2 += CString(iDigits - sVal.length(), '0');
                    sFormat2 += sVal;
                    uLastCopied = i + 1;
                    bInFormat = false;
                    break;
                }
                default:
                    bInFormat = false;
            }
        }
    }

    if (uLastCopied) {
        sFormat2 += sFormat.substr(uLastCopied);
        return FormatTime(tv.tv_sec, sFormat2, sTimezone);
    } else {
        // If there are no extended format specifiers, avoid doing any
        // memory allocations entirely.
        return FormatTime(tv.tv_sec, sFormat, sTimezone);
    }
}

CString CUtils::FormatServerTime(const timeval& tv) {
    CString s_msec(tv.tv_usec / 1000);
    while (s_msec.length() < 3) {
        s_msec = "0" + s_msec;
    }
    // TODO support leap seconds properly
    // TODO support message-tags properly
    struct tm stm;
    memset(&stm, 0, sizeof(stm));
    // OpenBSD has tv_sec as int, so explicitly convert it to time_t to make
    // gmtime_r() happy
    const time_t secs = tv.tv_sec;
    gmtime_r(&secs, &stm);
    char sTime[20] = {};
    strftime(sTime, sizeof(sTime), "%Y-%m-%dT%H:%M:%S", &stm);
    return CString(sTime) + "." + s_msec + "Z";
}

timeval CUtils::ParseServerTime(const CString& sTime) {
    struct tm stm;
    memset(&stm, 0, sizeof(stm));
    const char* cp = strptime(sTime.c_str(), "%Y-%m-%dT%H:%M:%S", &stm);
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    if (cp) {
        tv.tv_sec = mktime(&stm);
        CString s_usec(cp);
        if (s_usec.TrimPrefix(".") && s_usec.TrimSuffix("Z")) {
            tv.tv_usec = s_usec.ToULong() * 1000;
        }
    }
    return tv;
}

namespace {
void FillTimezones(const CString& sPath, SCString& result,
                   const CString& sPrefix) {
    CDir Dir;
    Dir.Fill(sPath);
    for (CFile* pFile : Dir) {
        CString sName = pFile->GetShortName();
        CString sFile = pFile->GetLongName();
        if (sName == "posix" || sName == "right")
            continue;  // these 2 dirs contain the same filenames
        if (sName.EndsWith(".tab") || sName == "posixrules" ||
            sName == "localtime")
            continue;
        if (pFile->IsDir()) {
            if (sName == "Etc") {
                FillTimezones(sFile, result, sPrefix);
            } else {
                FillTimezones(sFile, result, sPrefix + sName + "/");
            }
        } else {
            result.insert(sPrefix + sName);
        }
    }
}
}  // namespace

SCString CUtils::GetTimezones() {
    static SCString result;
    if (result.empty()) {
        FillTimezones("/usr/share/zoneinfo", result, "");
    }
    return result;
}

SCString CUtils::GetEncodings() {
    static SCString ssResult;
#ifdef HAVE_ICU
    if (ssResult.empty()) {
        for (int i = 0; i < ucnv_countAvailable(); ++i) {
            const char* pConvName = ucnv_getAvailableName(i);
            ssResult.insert(pConvName);
            icu::ErrorCode e;
            for (int st = 0; st < ucnv_countStandards(); ++st) {
                const char* pStdName = ucnv_getStandard(st, e);
                icu::LocalUEnumerationPointer ue(
                    ucnv_openStandardNames(pConvName, pStdName, e));
                while (const char* pStdConvNameEnum =
                           uenum_next(ue.getAlias(), nullptr, e)) {
                    ssResult.insert(pStdConvNameEnum);
                }
            }
        }
    }
#endif
    return ssResult;
}

bool CUtils::CheckCIDR(const CString& sIP, const CString& sRange) {
    if (sIP.WildCmp(sRange)) {
        return true;
    }
    auto deleter = [](addrinfo* ai) { freeaddrinfo(ai); };
    // Try to split the string into an IP and routing prefix
    VCString vsSplitCIDR;
    if (sRange.Split("/", vsSplitCIDR, false) != 2) return false;
    const CString sRoutingPrefix = vsSplitCIDR.back();
    const int iRoutingPrefix = sRoutingPrefix.ToInt();
    if (iRoutingPrefix < 0 || iRoutingPrefix > 128) return false;

    // If iRoutingPrefix is 0, it could be due to ToInt() failing, so
    // sRoutingPrefix needs to be all zeroes
    if (iRoutingPrefix == 0 && sRoutingPrefix != "0") return false;

    // Convert each IP from a numeric string to an addrinfo
    addrinfo aiHints;
    memset(&aiHints, 0, sizeof(addrinfo));
    aiHints.ai_flags = AI_NUMERICHOST;

    addrinfo* aiHostC;
    int iIsHostValid = getaddrinfo(sIP.c_str(), nullptr, &aiHints, &aiHostC);
    if (iIsHostValid != 0) return false;
    std::unique_ptr<addrinfo, decltype(deleter)> aiHost(aiHostC, deleter);

    aiHints.ai_family = aiHost->ai_family;  // Host and range must be in
                                            // the same address family

    addrinfo* aiRangeC;
    int iIsRangeValid =
        getaddrinfo(vsSplitCIDR.front().c_str(), nullptr, &aiHints, &aiRangeC);
    if (iIsRangeValid != 0) {
        return false;
    }
    std::unique_ptr<addrinfo, decltype(deleter)> aiRange(aiRangeC, deleter);

    // "/0" allows all IPv[4|6] addresses
    if (iRoutingPrefix == 0) {
        return true;
    }

    // If both IPs are valid and of the same type, make a bit field mask
    // from the routing prefix, AND it to the host and range, and see if
    // they match
    if (aiHost->ai_family == AF_INET) {
        if (iRoutingPrefix > 32) {
            return false;
        }

        const sockaddr_in* saHost =
            reinterpret_cast<const sockaddr_in*>(aiHost->ai_addr);
        const sockaddr_in* saRange =
            reinterpret_cast<const sockaddr_in*>(aiRange->ai_addr);

        // Make IPv4 bitmask
        const in_addr_t inBitmask = htonl((~0u) << (32 - iRoutingPrefix));

        // Compare masked IPv4s
        return ((inBitmask & saHost->sin_addr.s_addr) ==
                (inBitmask & saRange->sin_addr.s_addr));
    } else if (aiHost->ai_family == AF_INET6) {
        // Make IPv6 bitmask
        in6_addr in6aBitmask;
        memset(&in6aBitmask, 0, sizeof(in6aBitmask));

        for (int i = 0, iBitsLeft = iRoutingPrefix; iBitsLeft > 0;
             ++i, iBitsLeft -= 8) {
            if (iBitsLeft >= 8) {
                in6aBitmask.s6_addr[i] = (uint8_t)(~0u);
            } else {
                in6aBitmask.s6_addr[i] = (uint8_t)(~0u) << (8 - iBitsLeft);
            }
        }

        // Compare masked IPv6s
        const sockaddr_in6* sa6Host =
            reinterpret_cast<const sockaddr_in6*>(aiHost->ai_addr);
        const sockaddr_in6* sa6Range =
            reinterpret_cast<const sockaddr_in6*>(aiRange->ai_addr);

        for (int i = 0; i < 16; ++i) {
            if ((in6aBitmask.s6_addr[i] & sa6Host->sin6_addr.s6_addr[i]) !=
                (in6aBitmask.s6_addr[i] & sa6Range->sin6_addr.s6_addr[i])) {
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
}

MCString CUtils::GetMessageTags(const CString& sLine) {
    if (sLine.StartsWith("@")) {
        return CMessage(sLine).GetTags();
    }
    return MCString::EmptyMap;
}

void CUtils::SetMessageTags(CString& sLine, const MCString& mssTags) {
    CMessage Message(sLine);
    Message.SetTags(mssTags);
    sLine = Message.ToString();
}

bool CTable::AddColumn(const CString& sName) {
    for (const CString& sHeader : m_vsHeaders) {
        if (sHeader.Equals(sName)) {
            return false;
        }
    }

    m_vsHeaders.push_back(sName);
    m_msuWidths[sName] = sName.size();

    return true;
}

CTable::size_type CTable::AddRow() {
    // Don't add a row if no headers are defined
    if (m_vsHeaders.empty()) {
        return (size_type)-1;
    }

    // Add a vector with enough space for each column
    push_back(vector<CString>(m_vsHeaders.size()));
    return size() - 1;
}

bool CTable::SetCell(const CString& sColumn, const CString& sValue,
                     size_type uRowIdx) {
    if (uRowIdx == (size_type)~0) {
        if (empty()) {
            return false;
        }

        uRowIdx = size() - 1;
    }

    unsigned int uColIdx = GetColumnIndex(sColumn);

    if (uColIdx == (unsigned int)-1) return false;

    (*this)[uRowIdx][uColIdx] = sValue;

    if (m_msuWidths[sColumn] < sValue.size())
        m_msuWidths[sColumn] = sValue.size();

    return true;
}

bool CTable::GetLine(unsigned int uIdx, CString& sLine) const {
    std::stringstream ssRet;

    if (empty()) {
        return false;
    }

    if (uIdx == 1) {
        ssRet.fill(' ');
        ssRet << "| ";

        for (unsigned int a = 0; a < m_vsHeaders.size(); a++) {
            ssRet.width(GetColumnWidth(a));
            ssRet << std::left << m_vsHeaders[a];
            ssRet << ((a == m_vsHeaders.size() - 1) ? " |" : " | ");
        }

        sLine = ssRet.str();
        return true;
    } else if ((uIdx == 0) || (uIdx == 2) || (uIdx == (size() + 3))) {
        ssRet.fill('-');
        ssRet << "+-";

        for (unsigned int a = 0; a < m_vsHeaders.size(); a++) {
            ssRet.width(GetColumnWidth(a));
            ssRet << std::left << "-";
            ssRet << ((a == m_vsHeaders.size() - 1) ? "-+" : "-+-");
        }

        sLine = ssRet.str();
        return true;
    } else {
        uIdx -= 3;

        if (uIdx < size()) {
            const std::vector<CString>& mRow = (*this)[uIdx];
            ssRet.fill(' ');
            ssRet << "| ";

            for (unsigned int c = 0; c < m_vsHeaders.size(); c++) {
                ssRet.width(GetColumnWidth(c));
                ssRet << std::left << mRow[c];
                ssRet << ((c == m_vsHeaders.size() - 1) ? " |" : " | ");
            }

            sLine = ssRet.str();
            return true;
        }
    }

    return false;
}

unsigned int CTable::GetColumnIndex(const CString& sName) const {
    for (unsigned int i = 0; i < m_vsHeaders.size(); i++) {
        if (m_vsHeaders[i] == sName) return i;
    }

    DEBUG("CTable::GetColumnIndex(" + sName + ") failed");

    return (unsigned int)-1;
}

CString::size_type CTable::GetColumnWidth(unsigned int uIdx) const {
    if (uIdx >= m_vsHeaders.size()) {
        return 0;
    }

    const CString& sColName = m_vsHeaders[uIdx];
    std::map<CString, CString::size_type>::const_iterator it =
        m_msuWidths.find(sColName);

    if (it == m_msuWidths.end()) {
        // AddColumn() and SetCell() should make sure that we get a value :/
        return 0;
    }
    return it->second;
}

void CTable::Clear() {
    clear();
    m_vsHeaders.clear();
    m_msuWidths.clear();
}

#ifdef HAVE_LIBSSL
CBlowfish::CBlowfish(const CString& sPassword, int iEncrypt,
                     const CString& sIvec)
    : m_ivec((unsigned char*)calloc(sizeof(unsigned char), 8)),
      m_bkey(),
      m_iEncrypt(iEncrypt),
      m_num(0) {
    if (sIvec.length() >= 8) {
        memcpy(m_ivec, sIvec.data(), 8);
    }

    BF_set_key(&m_bkey, (unsigned int)sPassword.length(),
               (unsigned char*)sPassword.data());
}

CBlowfish::~CBlowfish() { free(m_ivec); }

//! output must be freed
unsigned char* CBlowfish::MD5(const unsigned char* input, unsigned int ilen) {
    unsigned char* output = (unsigned char*)malloc(MD5_DIGEST_LENGTH);
    ::MD5(input, ilen, output);
    return output;
}

//! returns an md5 of the CString (not hex encoded)
CString CBlowfish::MD5(const CString& sInput, bool bHexEncode) {
    CString sRet;
    unsigned char* data =
        MD5((const unsigned char*)sInput.data(), (unsigned int)sInput.length());

    if (!bHexEncode) {
        sRet.append((const char*)data, MD5_DIGEST_LENGTH);
    } else {
        for (int a = 0; a < MD5_DIGEST_LENGTH; a++) {
            sRet += g_HexDigits[data[a] >> 4];
            sRet += g_HexDigits[data[a] & 0xf];
        }
    }

    free(data);
    return sRet;
}

//! output must be the same size as input
void CBlowfish::Crypt(unsigned char* input, unsigned char* output,
                      unsigned int uBytes) {
    BF_cfb64_encrypt(input, output, uBytes, &m_bkey, m_ivec, &m_num,
                     m_iEncrypt);
}

//! must free result
unsigned char* CBlowfish::Crypt(unsigned char* input, unsigned int uBytes) {
    unsigned char* buff = (unsigned char*)malloc(uBytes);
    Crypt(input, buff, uBytes);
    return buff;
}

CString CBlowfish::Crypt(const CString& sData) {
    unsigned char* buff =
        Crypt((unsigned char*)sData.data(), (unsigned int)sData.length());
    CString sOutput;
    sOutput.append((const char*)buff, sData.length());
    free(buff);
    return sOutput;
}

#endif  // HAVE_LIBSSL
