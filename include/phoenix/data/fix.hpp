#pragma once

#include "phoenix/helpers/conversion.hpp"

#include <boost/unordered/unordered_flat_map.hpp>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <charconv>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Existing C++ implementations of FIX protocol were boring and overly complicated,
// so this is one that reduces copying and allocations, and assumes single-threaded use

namespace phoenix {

namespace concepts {
template<typename T>
concept Numerical = (std::integral<T> || std::floating_point<T>) && !std::same_as<T, char> && !std::same_as<T, bool>;
}

// Zero allocations after construction
// This will only be constructed on startup if FIXMessageBuilder is used
struct FIXBuilder
{
    FIXBuilder() { bodyBuffer.reserve(4096u); }

    FIXBuilder(FIXBuilder&&) = default;
    FIXBuilder& operator=(FIXBuilder&&) = default;

    FIXBuilder(FIXBuilder const&) = default;
    FIXBuilder& operator=(FIXBuilder const&) = default;

    inline void reset(std::size_t seqNum, std::string_view msgType, std::string_view client)
    {
        bodyBuffer.clear();
        bodyBuffer.resize(HEADER_BUFFER_SIZE);
        size = 0u;

        append("35", msgType);
        append("49", client);
        append("56", "DERIBITSERVER");
        append("34", seqNum);
    }

    inline void append(std::string_view tag, std::string_view value)
    {
        bodyBuffer.insert(bodyBuffer.end(), tag.begin(), tag.end());
        bodyBuffer.push_back('=');
        bodyBuffer.insert(bodyBuffer.end(), value.begin(), value.end());
        bodyBuffer.push_back(FIX_FIELD_DELIMITER);

        size += tag.size() + value.size() + 2;
    }

    // Node: requires null terminated string
    inline void append(std::string_view tag, char const* value)
    {
        bodyBuffer.insert(bodyBuffer.end(), tag.begin(), tag.end());
        bodyBuffer.push_back('=');

        for (std::size_t i = 0u; value[i] != '\0'; ++i)
        {
            bodyBuffer.push_back(value[i]);
            ++size;
        }

        bodyBuffer.push_back(FIX_FIELD_DELIMITER);
        size += tag.size() + 2;
    }

    inline void append(std::string_view tag, char value)
    {
        bodyBuffer.insert(bodyBuffer.end(), tag.begin(), tag.end());
        bodyBuffer.push_back('=');
        bodyBuffer.push_back(value);
        bodyBuffer.push_back(FIX_FIELD_DELIMITER);

        size += tag.size() + 3;
    }

    inline void append(std::string_view tag, bool value)
    {
        bodyBuffer.insert(bodyBuffer.end(), tag.begin(), tag.end());
        bodyBuffer.push_back('=');
        bodyBuffer.push_back(value ? 'Y' : 'N');
        bodyBuffer.push_back(FIX_FIELD_DELIMITER);

        size += tag.size() + 3;
    }

    inline void append(std::string_view tag, concepts::Numerical auto value)
    {
        bodyBuffer.insert(bodyBuffer.end(), tag.begin(), tag.end());
        bodyBuffer.push_back('=');

        char* ptr = numberBuffer;
        auto result = std::to_chars(ptr, ptr + sizeof(numberBuffer), value);
        bodyBuffer.insert(bodyBuffer.end(), ptr, result.ptr);

        bodyBuffer.push_back(FIX_FIELD_DELIMITER);

        size += tag.size() + (result.ptr - ptr) + 2;
    }

    inline std::string_view serialize()
    {
        // protocol field
        char* headerStart = headerBuffer;
        char* ptr = headerStart;

        auto const addCharToHeader = [&ptr](char c) { *ptr++ = c; };

        addCharToHeader('8');
        addCharToHeader('=');
        for (std::size_t i = 0; i < sizeof(FIX_PROTOCOL) - 1; ++i)
            addCharToHeader(FIX_PROTOCOL[i]);

        addCharToHeader(FIX_FIELD_DELIMITER);

        // length field
        addCharToHeader('9');
        addCharToHeader('=');
        auto result = std::to_chars(ptr, ptr + (sizeof(headerBuffer) - (ptr - headerStart)), size);
        ptr += result.ptr - ptr;
        addCharToHeader(FIX_FIELD_DELIMITER);

        std::size_t const totalHeaderLength = ptr - headerStart;

        // add header to bodyBuffer directly
        std::size_t const bufferHeaderStart = HEADER_BUFFER_SIZE - totalHeaderLength;
        for (std::size_t i = 0; i < totalHeaderLength; ++i)
        {
            std::size_t const bufferIdx = i + bufferHeaderStart;
            bodyBuffer[bufferIdx] = headerBuffer[i];
        }

        // checksum
        std::string_view checksum = calculateChecksum();
        bodyBuffer.push_back('1');
        bodyBuffer.push_back('0');
        bodyBuffer.push_back('=');
        bodyBuffer.insert(bodyBuffer.end(), checksum.begin(), checksum.end());
        bodyBuffer.push_back(FIX_FIELD_DELIMITER);

        return {&bodyBuffer[bufferHeaderStart], bodyBuffer.size() - bufferHeaderStart};
    }

    static constexpr char FIX_FIELD_DELIMITER = '\x01';
    static constexpr char FIX_PROTOCOL[] = "FIX.4.4";
    static constexpr std::size_t FIX_PROTOCOL_FIELD_LENGTH = sizeof(FIX_PROTOCOL) + 2u;

private:
    std::string_view calculateChecksum()
    {
        unsigned int checksum = 0;
        std::size_t const bodyEnd = bodyBuffer.size();
        for (std::size_t i = HEADER_BUFFER_SIZE; i < bodyEnd; ++i)
            checksum += static_cast<unsigned char>(bodyBuffer[i]);

        checksum %= 256;
        std::snprintf(checksumBuffer, sizeof(checksumBuffer), "%03d", checksum);
        return {checksumBuffer, 3};
    }

    char checksumBuffer[4];

    // header
    static constexpr std::uint64_t HEADER_BUFFER_SIZE = 32u; // 32 bytes reserved for header
    char headerBuffer[HEADER_BUFFER_SIZE];

    // body
    char numberBuffer[32];
    std::size_t size = 0u;
    std::vector<char> bodyBuffer;
};

struct FIXMessageBuilder
{
    FIXMessageBuilder(std::string_view client)
        : client{client}
    {}

    std::string_view login(std::size_t seqNum, std::string_view username, std::string_view secret, int heartbeatSeconds)
    {
        std::uint64_t timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();

        static constexpr int nonceSize = 64;
        unsigned char nonceBytes[nonceSize];
        if (!RAND_bytes(nonceBytes, nonceSize))
            throw(std::runtime_error("Error while creating random bytes"));

        unsigned char const* nonce = reinterpret_cast<unsigned char const*>(nonceBytes);
        std::string nonce64 = encodeBase64(nonce, nonceSize);

        std::stringstream rawDataStream;
        rawDataStream << timestamp << "." << nonce64;
        auto const rawData = rawDataStream.str();

        std::string rawAndSecret{rawData};
        rawAndSecret.append(secret);
        auto const passwordSHA256 = encodeSHA256(rawAndSecret);
        auto const password = encodeBase64(passwordSHA256.data(), passwordSHA256.size());

        builder.reset(seqNum, "A", client);
        builder.append("108", heartbeatSeconds);
        builder.append("96", rawData);
        builder.append("553", username);
        builder.append("554", password);

        // mass cancel on disconnect
        builder.append("9001", true);

        return builder.serialize();
    }

    inline std::string_view logout(std::size_t seqNum)
    {
        builder.reset(seqNum, "5", client);
        return builder.serialize();
    }

    inline std::string_view heartbeat(std::size_t seqNum, std::string_view testReqId)
    {
        builder.reset(seqNum, "0", client);
        builder.append("112", testReqId);
        return builder.serialize();
    }

    inline std::string_view heartbeat(std::size_t seqNum)
    {
        builder.reset(seqNum, "0", client);
        return builder.serialize();
    }

    inline std::string_view marketDataRequestTopLevel(std::size_t seqNum, std::string_view symbol)
    {
        builder.reset(seqNum, "V", client);
        builder.append("262", seqNum);
        builder.append("263", 0); // full refresh of 1 depth
        builder.append("264", 1);
        builder.append("55", symbol);
        builder.append("267", 2);
        builder.append("269", 0);
        builder.append("269", 1);
        return builder.serialize();
    }

    inline std::string_view marketDataRequestIncremental(std::size_t seqNum, std::string_view symbol)
    {
        builder.reset(seqNum, "V", client);
        builder.append("262", seqNum);
        builder.append("263", 1);
        builder.append("265", 1);
        builder.append("55", symbol);
        builder.append("267", 2);
        builder.append("269", 0);
        builder.append("269", 1);
        return builder.serialize();
    }

    inline std::string_view marketDataIncrementalTriple(std::size_t seqNum, std::vector<std::string> const& instruments)
    {
        builder.reset(seqNum, "V", client);
        builder.append("262", seqNum);
        builder.append("263", 1);
        builder.append("265", 1);

        builder.append("146", 3);
        for (auto const& symbol : instruments)
            builder.append("55", symbol);

        builder.append("267", 2);
        builder.append("269", 0);
        builder.append("269", 1);
        return builder.serialize();
    }

    inline std::string_view newOrderSingle(std::size_t seqNum, std::string_view symbol, auto& order)
    {
        builder.reset(seqNum, "D", client);

        char* seqPtr = seqNumBuffer;
        auto const addToSeqBuffer = [&seqPtr](char c) { *seqPtr++ = c; };
        if (order.takeProfit)
        {
            addToSeqBuffer('t');
            auto result = std::to_chars(seqPtr, seqPtr + sizeof(seqNumBuffer), seqNum);
            *result.ptr = '\0';
            builder.append("11", seqNumBuffer);
        }
        else
            builder.append("11", seqNum);

        builder.append("54", order.side);
        builder.append("38", order.volume.str());
        builder.append("44", order.price.str());
        builder.append("55", symbol);
        return builder.serialize();
    }

    inline std::string_view newMarketOrderSingle(std::size_t seqNum, auto const& order)
    {
        builder.reset(seqNum, "D", client);
        builder.append("11", seqNum);
        builder.append("40", 1);
        builder.append("44", order.price.str());
        builder.append("38", order.volume.str());
        builder.append("54", order.side);
        builder.append("55", order.symbol);
        builder.append("59", 4); // FOK
        return builder.serialize();
    }

    inline std::string_view orderCancelRequest(std::size_t seqNum, std::string_view symbol, std::string_view orderId)
    {
        builder.reset(seqNum, "F", client);
        builder.append("41", orderId);
        builder.append("55", symbol);
        return builder.serialize();
    }

    inline std::string_view requestForPositions(std::size_t seqNum)
    {
        builder.reset(seqNum, "AN", client);
        builder.append("710", seqNum);
        builder.append("724", 0);
        builder.append("263", 1);
        return builder.serialize();
    }

private:
    std::string encodeBase64(auto* bytes, unsigned int length)
    {
        std::string ret;
        int i = 0;
        int j = 0;
        unsigned char charArray3[3];
        unsigned char charArray4[4];

        while (length--)
        {
            charArray3[i++] = *(bytes++);
            if (i == 3)
            {
                charArray4[0] = (charArray3[0] & 0xfc) >> 2;
                charArray4[1] = ((charArray3[0] & 0x03) << 4) + ((charArray3[1] & 0xf0) >> 4);
                charArray4[2] = ((charArray3[1] & 0x0f) << 2) + ((charArray3[2] & 0xc0) >> 6);
                charArray4[3] = charArray3[2] & 0x3f;

                for (i = 0; (i < 4); i++)
                    ret += BASE_64_CHARS[charArray4[i]];
                i = 0;
            }
        }

        if (i)
        {
            for (j = i; j < 3; j++)
                charArray3[j] = '\0';

            charArray4[0] = (charArray3[0] & 0xfc) >> 2;
            charArray4[1] = ((charArray3[0] & 0x03) << 4) + ((charArray3[1] & 0xf0) >> 4);
            charArray4[2] = ((charArray3[1] & 0x0f) << 2) + ((charArray3[2] & 0xc0) >> 6);
            charArray4[3] = charArray3[2] & 0x3f;

            for (j = 0; (j < i + 1); j++)
                ret += BASE_64_CHARS[charArray4[j]];

            while ((i++ < 3))
                ret += '=';
        }

        return ret;
    }

    std::vector<unsigned char> encodeSHA256(std::string const& input)
    {
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if (context == nullptr)
            throw std::runtime_error("EVP_MD_CTX_new failed");

        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1)
        {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("EVP_DigestInit_ex failed");
        }

        if (EVP_DigestUpdate(context, input.c_str(), input.size()) != 1)
        {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("EVP_DigestUpdate failed");
        }

        std::vector<unsigned char> hash(EVP_MAX_MD_SIZE);
        unsigned int lengthOfHash = 0;
        if (EVP_DigestFinal_ex(context, hash.data(), &lengthOfHash) != 1)
        {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("EVP_DigestFinal_ex failed");
        }

        hash.resize(lengthOfHash);
        EVP_MD_CTX_free(context);
        return hash;
    }

    static constexpr char BASE_64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                            "abcdefghijklmnopqrstuvwxyz"
                                            "0123456789+/";

    char seqNumBuffer[32] = {'\0'};

    FIXBuilder builder;
    std::string client;
};

struct FIXReader
{
    FIXReader(std::string_view data);

    FIXReader(FIXReader&&) = default;
    FIXReader& operator=(FIXReader&&) = default;

    FIXReader(FIXReader&) = delete;
    FIXReader& operator=(FIXReader&) = delete;

    inline std::string const& getString(std::string const& tag, std::size_t index = 0u)
    {
        auto it = fields.find(tag);
        if (it != fields.end())
            if (it->second.size() > index)
                return it->second[index];
        return UNKNOWN;
    }

    inline std::string_view getStringView(std::string const& tag, std::size_t index = 0u)
    {
        auto it = fields.find(tag);
        if (it != fields.end())
            if (it->second.size() > index)
                return it->second[index];
        return UNKNOWN;
    }

    template<concepts::Numerical T>
    inline T getNumber(std::string const& tag, std::size_t index = 0u)
    {
        auto val = getStringView(tag, index);
        if (val == UNKNOWN)
            return 0u;

        T result;
        std::from_chars(val.data(), val.data() + val.size(), result);
        return result;
    }

    template<typename DecimalType>
    inline DecimalType getDecimal(std::string const& tag, std::size_t index = 0u)
    {
        auto val = getStringView(tag, index);
        if (val == UNKNOWN)
            return {};

        return {val};
    }

    inline bool getBool(std::string const& tag, std::size_t index = 0u)
    {
        auto val = getStringView(tag, index);
        if (val == UNKNOWN)
            return false;

        return val == "Y" || val == "y";
    }

    inline bool isMessageType(std::string_view msgType) { return this->msgType == msgType; }
    inline std::string const& getMessageType() { return msgType; }

    inline bool contains(std::string const& tag, std::size_t index = 0u)
    {
        auto it = fields.find(tag);
        if (it != fields.end())
            if (it->second.size() > index)
                return true;
        return false;
    }

    inline std::size_t getFieldSize(std::string const& tag) { return fields[tag].size(); }

    static constexpr std::string UNKNOWN = "UNKNOWN";

private:
    boost::unordered_flat_map<std::string, std::vector<std::string>> fields;
    std::string msgType;
};

} // namespace phoenix
