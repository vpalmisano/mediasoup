#define MS_CLASS "RTC::PortManager"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/PortManager.hpp"
#include "DepLibUV.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "Settings.hpp"
#include "Utils.hpp"
#include <tuple>   // std:make_tuple()
#include <utility> // std::piecewise_construct

/* Static methods for UV callbacks. */

static inline void onClose(uv_handle_t* handle)
{
	delete handle;
}

inline static void onFakeConnection(uv_stream_t* /*handle*/, int /*status*/)
{
	// Do nothing.
}

namespace RTC
{
	/* Class variables. */

	std::unordered_map<std::string, std::vector<bool>> PortManager::mapUdpIpPorts;
	std::unordered_map<std::string, std::vector<bool>> PortManager::mapTcpIpPorts;

	/* Class methods. */

	uv_handle_t* PortManager::Bind(Transport transport, std::string& ip, bool plain)
	{
		MS_TRACE();

		// First normalize the IP. This may throw if invalid IP.
		Utils::IP::NormalizeIp(ip);

		int err;
		int family = Utils::IP::GetFamily(ip);
		struct sockaddr_storage bindAddr; // NOLINT(cppcoreguidelines-pro-type-member-init)
		size_t portIdx;
		size_t portIdxOffset;
		int flags{ 0 };
		std::vector<bool>& ports = PortManager::GetPorts(transport, ip);
		size_t rtcPortsLength;
		size_t plainPortsLength{ 0u };
		size_t attempt{ 0u };
		size_t numAttempts = ports.size();
		uv_handle_t* uvHandle{ nullptr };
		uint16_t port;
		std::string transportStr;

		switch (transport)
		{
			case Transport::UDP:
				transportStr.assign("udp");
				break;

			case Transport::TCP:
				transportStr.assign("tcp");
				break;
		}

		switch (family)
		{
			case AF_INET:
			{
				err = uv_ip4_addr(ip.c_str(), 0, reinterpret_cast<struct sockaddr_in*>(&bindAddr));

				if (err != 0)
					MS_THROW_ERROR("uv_ip4_addr() failed: %s", uv_strerror(err));

				break;
			}

			case AF_INET6:
			{
				err = uv_ip6_addr(ip.c_str(), 0, reinterpret_cast<struct sockaddr_in6*>(&bindAddr));

				if (err != 0)
					MS_THROW_ERROR("uv_ip6_addr() failed: %s", uv_strerror(err));

				// Don't also bind into IPv4 when listening in IPv6.
				flags |= UV_UDP_IPV6ONLY;

				break;
			}

			// This cannot happen.
			default:
			{
				MS_THROW_ERROR("unknown IP family");
			}
		}

		// If a plain ports range is defined, set the length of the ports array devoted to plain ports allocations.
		if (Settings::configuration.plainMinPort > 0 && Settings::configuration.plainMaxPort > 0)
			plainPortsLength = Settings::configuration.plainMaxPort - Settings::configuration.plainMinPort; 

		// Set the available ports length.
		rtcPortsLength = ports.size() - plainPortsLength;

		// Choose a random port index to start from.
		if (plain && plainPortsLength)
		{
			portIdx = static_cast<size_t>(Utils::Crypto::GetRandomUInt(
		  	  static_cast<uint32_t>(0), static_cast<uint32_t>(plainPortsLength - 1)));
			
			portIdxOffset = rtcPortsLength;
		}
		else
		{
			portIdx = static_cast<size_t>(Utils::Crypto::GetRandomUInt(
		  	  static_cast<uint32_t>(0), static_cast<uint32_t>(rtcPortsLength - 1)));

			portIdxOffset = 0;
		}
		
		// Iterate all ports until getting one available. Fail if none found and also
		// if bind() fails N times in theorically available ports.
		while (true)
		{
			// Increase attempt number.
			++attempt;

			// If we have tried all the ports in the range throw.
			if (attempt > numAttempts)
			{
				MS_THROW_ERROR(
				  "no more available ports [transport:%s, ip:'%s', numAttempt:%zu]",
				  transportStr.c_str(),
				  ip.c_str(),
				  numAttempts);
			}

			if (plain && plainPortsLength)
			{
				// Increase current port index.
				portIdx = (portIdx + 1) %plainPortsLength;

				// So the corresponding port is the vector position plus the plain minimum port.
				port = static_cast<uint16_t>(portIdx + Settings::configuration.plainMinPort);
			}
			else
			{
				// Increase current port index.
				portIdx = (portIdx + 1) %rtcPortsLength;

				// So the corresponding port is the vector position plus the RTC minimum port.
				port = static_cast<uint16_t>(portIdx + Settings::configuration.rtcMinPort);
			}
			
			MS_DEBUG_DEV(
			  "testing port [transport:%s, ip:'%s', port:%" PRIu16 ", attempt:%zu/%zu]",
			  transportStr.c_str(),
			  ip.c_str(),
			  port,
			  attempt,
			  numAttempts);

			// Check whether this port is not available.
			if (ports[portIdx + portIdxOffset])
			{
				MS_DEBUG_DEV(
				  "port in use, trying again [transport:%s, ip:'%s', port:%" PRIu16 ", attempt:%zu/%zu]",
				  transportStr.c_str(),
				  ip.c_str(),
				  port,
				  attempt,
				  numAttempts);

				continue;
			}

			// Here we already have a theorically available port. Now let's check
			// whether no other process is binding into it.

			// Set the chosen port into the sockaddr struct.
			switch (family)
			{
				case AF_INET:
					(reinterpret_cast<struct sockaddr_in*>(&bindAddr))->sin_port = htons(port);
					break;

				case AF_INET6:
					(reinterpret_cast<struct sockaddr_in6*>(&bindAddr))->sin6_port = htons(port);
					break;
			}

			// Try to bind on it.
			switch (transport)
			{
				case Transport::UDP:
					uvHandle = reinterpret_cast<uv_handle_t*>(new uv_udp_t());
					err      = uv_udp_init_ex(
            DepLibUV::GetLoop(), reinterpret_cast<uv_udp_t*>(uvHandle), UV_UDP_RECVMMSG);
					break;

				case Transport::TCP:
					uvHandle = reinterpret_cast<uv_handle_t*>(new uv_tcp_t());
					err      = uv_tcp_init(DepLibUV::GetLoop(), reinterpret_cast<uv_tcp_t*>(uvHandle));
					break;
			}

			if (err != 0)
			{
				delete uvHandle;

				switch (transport)
				{
					case Transport::UDP:
						MS_THROW_ERROR("uv_udp_init_ex() failed: %s", uv_strerror(err));
						break;

					case Transport::TCP:
						MS_THROW_ERROR("uv_tcp_init() failed: %s", uv_strerror(err));
						break;
				}
			}

			switch (transport)
			{
				case Transport::UDP:
				{
					err = uv_udp_bind(
					  reinterpret_cast<uv_udp_t*>(uvHandle),
					  reinterpret_cast<const struct sockaddr*>(&bindAddr),
					  flags);

					if (err)
					{
						MS_WARN_DEV(
						  "uv_udp_bind() failed [transport:%s, ip:'%s', port:%" PRIu16 ", attempt:%zu/%zu]: %s",
						  transportStr.c_str(),
						  ip.c_str(),
						  port,
						  attempt,
						  numAttempts,
						  uv_strerror(err));
					}

					break;
				}

				case Transport::TCP:
				{
					err = uv_tcp_bind(
					  reinterpret_cast<uv_tcp_t*>(uvHandle),
					  reinterpret_cast<const struct sockaddr*>(&bindAddr),
					  flags);

					if (err)
					{
						MS_WARN_DEV(
						  "uv_tcp_bind() failed [transport:%s, ip:'%s', port:%" PRIu16 ", attempt:%zu/%zu]: %s",
						  transportStr.c_str(),
						  ip.c_str(),
						  port,
						  attempt,
						  numAttempts,
						  uv_strerror(err));
					}

					// uv_tcp_bind() may succeed even if later uv_listen() fails, so
					// double check it.
					if (err == 0)
					{
						err = uv_listen(
						  reinterpret_cast<uv_stream_t*>(uvHandle),
						  256,
						  static_cast<uv_connection_cb>(onFakeConnection));

						MS_WARN_DEV(
						  "uv_listen() failed [transport:%s, ip:'%s', port:%" PRIu16 ", attempt:%zu/%zu]: %s",
						  transportStr.c_str(),
						  ip.c_str(),
						  port,
						  attempt,
						  numAttempts,
						  uv_strerror(err));
					}

					break;
				}
			}

			// If it succeeded, exit the loop here.
			if (err == 0)
				break;

			// If it failed, close the handle and check the reason.
			uv_close(reinterpret_cast<uv_handle_t*>(uvHandle), static_cast<uv_close_cb>(onClose));

			switch (err)
			{
				// If bind() fails due to "too many open files" just throw.
				case UV_EMFILE:
				{
					MS_THROW_ERROR(
					  "port bind failed due to too many open files [transport:%s, ip:'%s', port:%" PRIu16
					  ", attempt:%zu/%zu]",
					  transportStr.c_str(),
					  ip.c_str(),
					  port,
					  attempt,
					  numAttempts);

					break;
				}

				// If cannot bind in the given IP, throw.
				case UV_EADDRNOTAVAIL:
				{
					MS_THROW_ERROR(
					  "port bind failed due to address not available [transport:%s, ip:'%s', port:%" PRIu16
					  ", attempt:%zu/%zu]",
					  transportStr.c_str(),
					  ip.c_str(),
					  port,
					  attempt,
					  numAttempts);

					break;
				}

				default:
				{
					// Otherwise continue in the loop to try again with next port.
				}
			}
		}

		// If here, we got an available port. Mark it as unavailable.
		ports[portIdx + portIdxOffset] = true;

		MS_DEBUG_DEV(
		  "bind succeeded [transport:%s, ip:'%s', port:%" PRIu16 ", attempt:%zu/%zu]",
		  transportStr.c_str(),
		  ip.c_str(),
		  port,
		  attempt,
		  numAttempts);

		return static_cast<uv_handle_t*>(uvHandle);
	}

	void PortManager::Unbind(Transport transport, std::string& ip, uint16_t port, bool plain)
	{
		MS_TRACE();
		
		size_t plainPortsLength{ 0u };

		// If a plain ports range is defined, set the length of the ports array devoted to plain ports allocations.
		if (Settings::configuration.plainMinPort > 0 && Settings::configuration.plainMaxPort > 0)
			plainPortsLength = Settings::configuration.plainMaxPort - Settings::configuration.plainMinPort; 

		if (plain && plainPortsLength) {
			if (
			  (static_cast<size_t>(port) < Settings::configuration.plainMinPort) ||
			  (static_cast<size_t>(port) > Settings::configuration.plainMaxPort))
			{
				MS_ERROR("given port %" PRIu16 " is out of plain ports range", port);

				return;
			}
		}
		else 
		{
			if (
			  (static_cast<size_t>(port) < Settings::configuration.rtcMinPort) ||
			  (static_cast<size_t>(port) > Settings::configuration.rtcMaxPort))
			{
				MS_ERROR("given port %" PRIu16 " is out of range", port);

				return;
			}			
		}

		size_t portIdx;
		if (plain && plainPortsLength)
			portIdx = static_cast<size_t>(port) - Settings::configuration.plainMinPort;
		else
			portIdx = static_cast<size_t>(port) - Settings::configuration.rtcMinPort;

		switch (transport)
		{
			case Transport::UDP:
			{
				auto it = PortManager::mapUdpIpPorts.find(ip);

				if (it == PortManager::mapUdpIpPorts.end())
					return;

				auto& ports = it->second;

				// Set the available ports length.
				size_t rtcPortsLength = ports.size() - plainPortsLength;

				// Mark the port as available.
				if (plain && plainPortsLength)
					ports[portIdx + rtcPortsLength] = false;
				else
					ports[portIdx] = false;

				break;
			}

			case Transport::TCP:
			{
				auto it = PortManager::mapTcpIpPorts.find(ip);

				if (it == PortManager::mapTcpIpPorts.end())
					return;

				auto& ports = it->second;

				// Set the available ports length.
				size_t rtcPortsLength = ports.size() - plainPortsLength;

				// Mark the port as available.
				if (plain && plainPortsLength)
					ports[portIdx + rtcPortsLength] = false;
				else
					ports[portIdx] = false;

				break;
			}
		}
	}

	std::vector<bool>& PortManager::GetPorts(Transport transport, const std::string& ip)
	{
		MS_TRACE();

		// Make GCC happy so it does not print:
		// "control reaches end of non-void function [-Wreturn-type]"
		static std::vector<bool> emptyPorts;

		switch (transport)
		{
			case Transport::UDP:
			{
				auto it = PortManager::mapUdpIpPorts.find(ip);

				// If the IP is already handled, return its ports vector.
				if (it != PortManager::mapUdpIpPorts.end())
				{
					auto& ports = it->second;

					return ports;
				}

				// Otherwise add an entry in the map and return it.
				uint16_t numPorts =
				  Settings::configuration.rtcMaxPort - Settings::configuration.rtcMinPort + 1;

				if (Settings::configuration.plainMinPort > 0 && Settings::configuration.plainMaxPort > 0)
					numPorts += Settings::configuration.plainMaxPort - Settings::configuration.plainMinPort + 1;

				// Emplace a new vector filled with numPorts false values, meaning that
				// all ports are available.
				auto pair = PortManager::mapUdpIpPorts.emplace(
				  std::piecewise_construct, std::make_tuple(ip), std::make_tuple(numPorts, false));

				// pair.first is an iterator to the inserted value.
				auto& ports = pair.first->second;

				return ports;
			}

			case Transport::TCP:
			{
				auto it = PortManager::mapTcpIpPorts.find(ip);

				// If the IP is already handled, return its ports vector.
				if (it != PortManager::mapTcpIpPorts.end())
				{
					auto& ports = it->second;

					return ports;
				}

				// Otherwise add an entry in the map and return it.
				uint16_t numPorts =
				  Settings::configuration.rtcMaxPort - Settings::configuration.rtcMinPort + 1;
				
				if (Settings::configuration.plainMinPort > 0 && Settings::configuration.plainMaxPort > 0)
					numPorts += Settings::configuration.plainMaxPort - Settings::configuration.plainMinPort + 1;

				// Emplace a new vector filled with numPorts false values, meaning that
				// all ports are available.
				auto pair = PortManager::mapTcpIpPorts.emplace(
				  std::piecewise_construct, std::make_tuple(ip), std::make_tuple(numPorts, false));

				// pair.first is an iterator to the inserted value.
				auto& ports = pair.first->second;

				return ports;
			}
		}

		return emptyPorts;
	}

	void PortManager::FillJson(json& jsonObject)
	{
		MS_TRACE();

		size_t plainPortsLength{ 0u };

		// Set the available ports length.
		if (Settings::configuration.plainMinPort > 0 && Settings::configuration.plainMaxPort > 0)
			plainPortsLength = Settings::configuration.plainMaxPort - Settings::configuration.plainMinPort;

		// Add udp.
		jsonObject["udp"] = json::object();
		auto jsonUdpIt    = jsonObject.find("udp");

		for (auto& kv : PortManager::mapUdpIpPorts)
		{
			const auto& ip = kv.first;
			auto& ports    = kv.second;

			(*jsonUdpIt)[ip] = json::array();
			auto jsonIpIt    = jsonUdpIt->find(ip);
		
			size_t rtcPortsLength = ports.size() - plainPortsLength;

			for (size_t i{ 0 }; i < ports.size(); ++i)
			{
				if (!ports[i])
					continue;

				uint16_t port;

				// Note: if a plain ports range is defined, the rtcPortsLength is < ports.size()
				if (i >= rtcPortsLength)
					// When the iteration reaches the rtcPortsLength index, we have a plain port allocation:
					port = static_cast<uint16_t>(i + Settings::configuration.plainMinPort);
				else
					// else, use the rtcMinPort value.
					port = static_cast<uint16_t>(i + Settings::configuration.rtcMinPort);

				jsonIpIt->push_back(port);
			}
		}

		// Add tcp.
		jsonObject["tcp"] = json::object();
		auto jsonTcpIt    = jsonObject.find("tcp");

		for (auto& kv : PortManager::mapTcpIpPorts)
		{
			const auto& ip = kv.first;
			auto& ports    = kv.second;

			(*jsonTcpIt)[ip] = json::array();
			auto jsonIpIt    = jsonTcpIt->find(ip);

			size_t rtcPortsLength = ports.size() - plainPortsLength;

			for (size_t i{ 0 }; i < ports.size(); ++i)
			{
				if (!ports[i])
					continue;

				uint16_t port;

				// Note: if a plain ports range is defined, the rtcPortsLength is < ports.size()
				if (i >= rtcPortsLength)
					// When the iteration reaches the rtcPortsLength index, we have a plain port allocation:
					port = static_cast<uint16_t>(i + Settings::configuration.plainMinPort);
				else
					// else, use the rtcMinPort value.
					port = static_cast<uint16_t>(i + Settings::configuration.rtcMinPort);

				jsonIpIt->emplace_back(port);
			}
		}
	}
} // namespace RTC
