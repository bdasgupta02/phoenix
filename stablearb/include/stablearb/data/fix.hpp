#pragma once

#include "stablearb/helpers/conversion.hpp"

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

namespace stablearb {

namespace concepts {
template<typename T>
concept Numerical = (std::integral<T> || std::floating_point<T>) && !std::same_as<T, char> && !std::same_as<T, bool>;
}

// Largely unoptimized for now
struct FIXBuilder
{
    FIXBuilder()
    {
        staging.reserve(8192u);
        buffer.reserve(8192u);
    }

    FIXBuilder(FIXBuilder&&) = default;
    FIXBuilder& operator=(FIXBuilder&&) = default;

    FIXBuilder(FIXBuilder const&) = default;
    FIXBuilder& operator=(FIXBuilder const&) = default;

    inline void reset(std::size_t seqNum, char msgType)
    {
        buffer.clear();
        staging.clear();
        size = 0u;

        staging.push_back({"8", FIX_PROTOCOL});
        staging.push_back({"9", ""});

        append("35", msgType);
        append("49", "algo");
        append("56", "DERIBITSERVER");
        append("34", seqNum);
    }

    inline void reset(std::size_t seqNum, std::string_view msgType)
    {
        buffer.clear();
        staging.clear();
        size = 0u;

        staging.push_back({"8", FIX_PROTOCOL});
        staging.push_back({"9", ""});

        append("35", msgType);
        append("49", "algo");
        append("56", "DERIBITSERVER");
        append("34", seqNum);
    }

    inline void append(std::string_view tag, std::string_view value)
    {
        size += tag.size() + value.size() + 2u;
        staging.push_back({std::string{tag}, std::string{value}});
    }

    inline void append(std::string_view tag, concepts::Numerical auto value)
    {
        static char convBuffer[64];
        static char* ptr;
        ptr = convBuffer;

        auto const result = std::to_chars(ptr, ptr + sizeof(convBuffer), value);
        std::string_view str(ptr, result.ptr - ptr);

        append(tag, str);
    }

    inline void append(std::string_view tag, bool value) { append(std::string{tag}, std::string{value ? "Y" : "N"}); }
    inline void append(std::string_view tag, char value) { append(std::string{tag}, std::string{&value, 1u}); }
    inline void append(std::string_view tag, char const* value) { append(std::string{tag}, std::string{value}); }

    inline std::string_view serialize()
    {
        std::string lengthStr = std::to_string(size);
        staging[1] = {"9", lengthStr};

        for (auto [tag, val] : staging)
        {
            buffer.insert(buffer.end(), tag.begin(), tag.end());
            buffer.push_back('=');
            buffer.insert(buffer.end(), val.begin(), val.end());
            buffer.push_back(FIX_FIELD_DELIMITER);
        }

        std::string_view checksum = calculateChecksum();
        buffer.push_back('1');
        buffer.push_back('0');
        buffer.push_back('=');
        buffer.insert(buffer.end(), checksum.begin(), checksum.end());
        buffer.push_back(FIX_FIELD_DELIMITER);

        return {buffer.data(), buffer.size()};
    }

    static constexpr char FIX_FIELD_DELIMITER = '\x01';
    static constexpr char FIX_PROTOCOL[] = "FIX.4.4";
    static constexpr std::size_t FIX_PROTOCOL_FIELD_LENGTH = sizeof(FIX_PROTOCOL) + 2u;

private:
    std::string_view calculateChecksum()
    {
        unsigned int checksum = 0;
        auto size = buffer.size();
        for (std::size_t i = 0u; i < size; ++i)
            checksum += static_cast<unsigned char>(buffer[i]);

        checksum %= 256;

        static char checksumStr[4];
        std::snprintf(checksumStr, sizeof(checksumStr), "%03d", checksum);
        return {checksumStr, 3};
    }

    std::vector<std::pair<std::string, std::string>> staging;
    std::vector<char> buffer;
    std::size_t size = 0u;
};

struct FIXMessageBuilder
{

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

        builder.reset(seqNum, 'A');
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
        builder.reset(seqNum, '5');
        return builder.serialize();
    }

    inline std::string_view heartbeat(std::size_t seqNum, std::string_view testReqId)
    {
        builder.reset(seqNum, '0');
        builder.append("112", testReqId);
        return builder.serialize();
    }

    inline std::string_view marketDataRequestTopLevel(std::size_t seqNum, std::string_view symbol)
    {
        builder.reset(seqNum, 'V');
        builder.append("262", seqNum);
        builder.append("263", 0); // full refresh of 1 depth
        builder.append("264", 1);
        builder.append("55", symbol);
        builder.append("267", 3);
        builder.append("269", 0);
        builder.append("269", 1);
        builder.append("269", 3);
        return builder.serialize();
    }

    inline std::string_view marketDataRequestIncremental(std::size_t seqNum, std::string_view symbol)
    {
        builder.reset(seqNum, 'V');
        builder.append("262", seqNum);
        builder.append("263", 1);
        builder.append("265", 1);
        builder.append("55", symbol);
        builder.append("267", 3);
        builder.append("269", 0);
        builder.append("269", 1);
        builder.append("269", 3);
        return builder.serialize();
    }

    inline std::string_view
        newOrderSingle(std::size_t seqNum, std::string_view symbol, auto& quote, bool takeProfit = false)
    {
        builder.reset(seqNum, 'D');

        if (takeProfit)
        {
            std::string clOrderId = "t" + std::to_string(seqNum);
            builder.append("11", clOrderId);
        }
        else
            builder.append("11", seqNum);

        builder.append("54", quote.side);
        builder.append("38", quote.volume.str());
        builder.append("44", quote.price.str());
        builder.append("55", symbol);
        return builder.serialize();
    }

    inline std::string_view requestForPositions(std::size_t seqNum)
    {
        builder.reset(seqNum, "AN");
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

    FIXBuilder builder;
};

struct FIXReader
{
    FIXReader(std::string_view data);

    FIXReader(FIXReader&&) = default;
    FIXReader& operator=(FIXReader&&) = default;

    FIXReader(FIXReader&) = delete;
    FIXReader& operator=(FIXReader&) = delete;

    inline std::string getString(std::string const& tag, std::size_t index = 0u)
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
        auto val = fields[tag][index];
        T result;
        std::from_chars(val.data(), val.data() + val.size(), result);
        return result;
    }

    template<typename DecimalType>
    inline DecimalType getDecimal(std::string const& tag, std::size_t index = 0u)
    {
        std::string_view str = getStringView(tag, index);
        return {str};
    }

    inline bool getBool(std::string const& tag, std::size_t index = 0u)
    {
        auto val = getStringView(tag, index);
        return val == "Y" || val == "y";
    }

    inline bool isMessageType(std::string_view msgType) { return this->msgType == msgType; }

    inline bool contains(std::string const& tag, std::size_t index = 0u)
    {
        auto it = fields.find(tag);
        if (it != fields.end())
            if (it->second.size() > index)
                return true;
        return false;
    }

    static constexpr std::string UNKNOWN = "UNKNOWN";

private:
    boost::unordered_flat_map<std::string, std::vector<std::string>> fields;
    std::string msgType;
};

} // namespace stablearb
