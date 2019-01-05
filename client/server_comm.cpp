#include "server_comm.h"
#include "main.h"
#include "server_comm_handlers.h"
#include "../sdk/framework/utils/crc32.h"
#include "../sdk/framework/utils/threading.h"
#include <stdio.h>
#include <string.h>

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <sstream>

typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

client wsocket;
websocketpp::connection_hdl curConnection;
X509* global_cert = nullptr;

using CommandCallbackFn = void(*)(const std::string& str);

struct ServerCommand
{
	crcs_t crc;
	CommandCallbackFn callback;
};

static ServerCommand commandList[] =
{
	{"lga"_crc32, LoginApproved},
	{"lgr"_crc32, LoginRejected},
	{"clr"_crc32, CheatLibraryReceive},
	{"la"_crc32, LibraryAllocate},
	{"slr"_crc32, SettingsLibraryReceive},
	{"sul"_crc32, SubscriptionList},
};

static auto cert_buf = ST(
	"-----BEGIN CERTIFICATE-----\n"
	"MIIFYzCCA0ugAwIBAgIUSo8g8efZJ4rpaKu1YodFOvshqRkwDQYJKoZIhvcNAQEL"
	"BQAwQTELMAkGA1UEBhMCTFQxEzARBgNVBAgMClNvbWUtU3RhdGUxHTAbBgNVBAoM"
	"FG0wZHVsYXIgdGVjaG5vbG9naWVzMB4XDTE5MDEwMzEyMzg1M1oXDTI4MTIzMTEy"
	"Mzg1M1owQTELMAkGA1UEBhMCTFQxEzARBgNVBAgMClNvbWUtU3RhdGUxHTAbBgNV"
	"BAoMFG0wZHVsYXIgdGVjaG5vbG9naWVzMIICIjANBgkqhkiG9w0BAQEFAAOCAg8A"
	"MIICCgKCAgEA7naP9P3V44+fKHG5DxwHEdaaM8KajZvgxbZYQ/9snzdwV3vmZFiA"
	"47uV9ij3pAIBYopwCSnKgc7QN/M09AiCnbxI0wUQTgfO8wRZtf0OyJeNQPkAD5JW"
	"9ar03XnGAe6DZ6tj+uz6Gl39X4AKZmJGM4LL5tPzLtpNcua5rks8+8qc3R/g3105"
	"7AManZiOLvK7YxazyauBJvo6pEXn3SrRzyLJYVMlUhfWWQE2BX0rSKbuSJ+uR3Uo"
	"Ci5BCbOJZG2pqRmbm2fd2i3u5DVtoFWNN/BT5XeZCdJjDGYKGOZpw59bWkMT5m8x"
	"bXY+2YvIcIO6KkNtlCByh9LVGb55Xe78bAaB+5Gp0/mHL2lXYFo3KkaKRuratuNx"
	"1Ap7ekIuABLrjEdUT+CFrmjGFbK3e+LmGhILc8yvtagDxPVOhytgdWDV08N0U3U7"
	"DLa+Sn1h84hNUsNxQYhuCFC81EKX7Xv+w7tTX2MljqkIQMn+H6w4/ehd4VIqdubD"
	"YpPDlbxgpBlQgl47Rao0KUznruaLewTG+Rw6QDl7dC/nIWoPwLqtM/Q5K4DsSBDd"
	"d/Zs8PfcURRBdyWnPk3ESYUiqNwWoCCNL2OlYh54lSq4k2AwIv9biHsvMqh1q/Pk"
	"lruqOveilO4O9NrsCbbONlHQElq3CsWSOtvhReDuLpucb+AoF/vvWZ0CAwEAAaNT"
	"MFEwHQYDVR0OBBYEFBvQR57La8zA7m9/RD8k4xXnM+swMB8GA1UdIwQYMBaAFBvQ"
	"R57La8zA7m9/RD8k4xXnM+swMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEL"
	"BQADggIBAAK2xc7Q9QCKwBkEQ42cM09NC1CobNxIoBjj9gLwWZaOx3DNldUWzoJh"
	"5dxM7jp+mRqB1WYcn/RQcSH6NwisXo7oNmA1lmL0TEUAPgKGw0YonDdNPTQKCyyG"
	"9BL30Qm1jDHolQOLPQUmfDD3vVNOLr3lOuI5FgHCrbizLQhCB2/u5hiQkGIEBxjw"
	"oTDbcoBCpZ2cbxQ/q4xvzubQvYjxKb8aV+5kKYGKMPZ8kKQtiC4GSkngFRzS0NBA"
	"M6PuxoX79RQN0pMBp5Ay2b5Eh4Mx2UGxwP0cOpDTT0IAioO38hi8Bd+W+tsYrVoX"
	"z4LGDxWU65K/xOq/z6mAc//VHrTcsfUW8mSHzc6U4Mm1CTkbpyS6MYtFSvYI4fug"
	"PWkRADu2sXgQl4d73EQsCrq8Sa72VDk4lV9MQLxJdgZuSu5T33w6dGjZBYQsgSpi"
	"jgrN75Fu4OAMCeDjVj8lgQjmZA8a9woLHrhK2YW7QJ+4cIlt5eqmMi8W8PiUpwgy"
	"d1NnhTplOEN+PxUFsy7ahBJ1EEA5/AgmC1ap1UandTrPZdbyWGWzN1wAxzdcVpYS"
	"rkJKZwilcapoZAHEJZrH+W74apuBkNjQwQ3Va/E3vFT8lEJG32RWykS8/AvjgFsW"
	"xDiuT9xNl0h1Y6aPLh7QOSfG6K4ruxziRAa6FDqEBZb9KR16fhAj\n"
	"-----END CERTIFICATE-----");

bool printed = false;
bool ServerComm::connected = false;
Semaphore ServerComm::mainsem;

void ServerComm::LoginCredentials()
{
	STPRINT("Enter your username and password:\nUsername: ");

	char username[64];
	char password[64];

	EchoInput(username, 64, 0);
	STPRINT("Password: ");
	EchoInput(password, 64, '*');

	char buf[256];

	sprintf(buf, "login\n%s\n%s\n", username, password);

	Send(buf);
}

void ExecuteCommand(crcs_t crc, const std::string& str)
{
	for (auto& i : commandList)
		if (i.crc == crc)
			i.callback(str);
}

void OnOpen(websocketpp::connection_hdl hdl)
{
	curConnection = hdl;
	if (!ServerComm::connected)
		ServerComm::mainsem.Post();
}

void OnMessage(websocketpp::connection_hdl, client::message_ptr msg)
{
	std::string str = msg->get_payload();
	//Decode the command from the server
	char command[128];
	sscanf(str.c_str(), "%s", command);
	command[127] = '\0';
	size_t flen = str.find("\n");
	if (flen < str.size())
		str.erase(0, flen + 1);
	else
	    str = "";
	int len = strlen(command);
	crcs_t crc = Crc32(command, len);
	ExecuteCommand(crc, str);
}

bool VerifyCertificate(const char* hostname, bool preverified, boost::asio::ssl::verify_context& ctx)
{
	X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
	return !X509_cmp(cert, global_cert);
}

context_ptr OnTlsInit(const char* hostname, websocketpp::connection_hdl)
{
	context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

	try {
		ctx->set_options(boost::asio::ssl::context::default_workarounds |
						 boost::asio::ssl::context::no_sslv2 |
						 boost::asio::ssl::context::no_sslv3 |
						 boost::asio::ssl::context::single_dh_use);


		ctx->set_verify_mode(boost::asio::ssl::verify_peer);
		ctx->set_verify_callback(bind(&VerifyCertificate, hostname, ::_1, ::_2));
	} catch (std::exception& e) {
		printf("%s\n", e.what());
	}
	return ctx;
}

void* __stdcall SockThread(void*)
{
	wsocket.run();
}

void ServerComm::Initialize()
{
	BIO* cbio = BIO_new_mem_buf((void*)cert_buf, -1);
	global_cert = nullptr;
	PEM_read_bio_X509(cbio, &global_cert, 0, nullptr);

	wsocket.set_access_channels(websocketpp::log::alevel::none);
	wsocket.clear_access_channels(websocketpp::log::alevel::frame_payload);
	wsocket.set_error_channels(websocketpp::log::elevel::none);

	wsocket.init_asio();

	std::string hostname = (char*)ST("192.168.122.1");
	std::string port = (char*)ST("8083");
	std::string uri = (char*)ST("wss://");
	uri = uri + hostname + ":" + port;

	wsocket.set_message_handler(OnMessage);
	wsocket.set_open_handler(OnOpen);
	wsocket.set_tls_init_handler(bind(&OnTlsInit, hostname.c_str(), ::_1));

	websocketpp::lib::error_code ec;
	client::connection_ptr con = wsocket.get_connection(uri, ec);
	if (ec) {
		STPRINT("could not create connection because: ");
		printf("%s\n", ec.message().c_str());
		return;
	}

	wsocket.connect(con);

	STPRINT("Connecting to the server...\n");

	Threading::StartThread(SockThread, nullptr);

	BIO_free(cbio);

	while (1) {
		mainsem.Wait();
		if (connected)
			break;
		ServerComm::LoginCredentials();
	}
}

void ServerComm::Stop()
{
}

void ServerComm::Send(const std::string& buf)
{
	wsocket.send(curConnection, buf, websocketpp::frame::opcode::binary);
}