#pragma once
#include <boost/asio/ip/address.hpp>
#include <stdexcept>
#include <string>
namespace clipstream::utils {
inline std::string normalize_ip(const std::string &value) {
    if (value.empty() || value.size() > 45 || value.find('/') != std::string::npos ||
        value.find('%') != std::string::npos)
        throw std::invalid_argument("An exact IPv4 or IPv6 address is required; CIDR is not supported");
    boost::system::error_code error;
    auto address = boost::asio::ip::make_address(value, error);
    if (error)
        throw std::invalid_argument("Invalid IP address");
    if (address.is_v6() && address.to_v6().is_v4_mapped()) {
        auto bytes = address.to_v6().to_bytes();
        boost::asio::ip::address_v4::bytes_type ipv4{bytes[12], bytes[13], bytes[14], bytes[15]};
        return boost::asio::ip::address_v4(ipv4).to_string();
    }
    return address.to_string();
}
} // namespace clipstream::utils
