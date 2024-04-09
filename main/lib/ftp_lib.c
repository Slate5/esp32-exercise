#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_err.h>
#include "esp_err_ext.h"

/*
 * IP and port max lengths as strings. These are used when FTP
 * enables the passive mode and provides IP and port for data transfer.
 */
#define IP_LEN 40
#define PORT_LEN 7


static const char *TAG = "ftp_lib";

/*
 * During initiation, store sockaddr elements in this global
 * struct to omit executing getaddrinfo() for every transfer.
 */
static struct __attribute__ ((packed)) conn_info {
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	const char *ip;
	const char *user;
	const char *pass;
} g_conn_info;


static int ftp_connect()
{
	int ret, sockfd;

	sockfd = socket(g_conn_info.ai_family,
		g_conn_info.ai_socktype,
		g_conn_info.ai_protocol);

	if (sockfd != -1) {
		ret = connect(sockfd, g_conn_info.ai_addr, g_conn_info.ai_addrlen);
		if (0 != ret) {
			ESP_LOGE(TAG, "Failed to connect to the FTP server");
			close(sockfd);
			sockfd = -1;
		}
	} else {
		ESP_LOGE(TAG, "Failed to open a socket");
	}

	return sockfd;
}

static int getaddrinfo_tryconnect(struct addrinfo *hints,
				struct addrinfo **result,
				const char *ip,
				const char *port)
{
	int sockfd;

	if (getaddrinfo(ip, port, hints, result) != 0) {
		ESP_LOGE(TAG, "Could not resolve domain name or IP is invalid");
		return -1;
	}

	for (; *result != NULL; *result = (*result)->ai_next) {
		sockfd = socket((*result)->ai_family,
				(*result)->ai_socktype,
				(*result)->ai_protocol);

		if (sockfd != -1) {
			if (connect(sockfd, (*result)->ai_addr, (*result)->ai_addrlen) == 0) {
				return sockfd;
			}

			close(sockfd);
		}
	}

	ESP_LOGE(TAG, "Failed to connect to FTP");
	return -1;
}

static esp_err_t ftp_send_command(int sockfd, const char *command, const char *args, bool verbose)
{
	size_t input_len = 4;

	if (command) {
		input_len += strlen(command);
	} else {
		ESP_LOGE(TAG, "Command is not provided");
		return ESP_FAIL;
	}
	if (args) {
		input_len += strlen(args);
	}

	char buffer[input_len];

	if (args) {
		snprintf(buffer, sizeof(buffer), "%s %s\r\n", command, args);
	} else {
		snprintf(buffer, sizeof(buffer), "%s\r\n", command);
	}

	if (send(sockfd, buffer, strlen(buffer), 0) == -1) {
		ESP_LOGE(TAG, "Failed to send a command");

		return ESP_FAIL;
	} else if (verbose) {
		if (!strcmp(command, "PASS")) {
			ESP_LOGI(TAG, "FTP< PASS %.*s", strlen(args),
				 "********************************");
		} else {
			ESP_LOGI(TAG, "FTP< %.*s", strlen(buffer) - 2, buffer);
		}
	}

	return ESP_OK;
}

static void ftp_receive_response(int sockfd)
{
	ssize_t ret;
	char buffer[256] = {0};

	for (uint8_t i = 0; i < 50; ++i) {
		ret = recv(sockfd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
		if (ret >= 0) {
			ESP_LOGI(TAG, "FTP> %s", buffer);
			return;
		}

		vTaskDelay(pdMS_TO_TICKS(100));
	}

	ESP_LOGW(TAG, "Resource temporarily unavailable");
}

/*
 * Parse FTP transfer addresses for both IPv4 and IPv6. IPv4 addresses always
 * follow the format 'h1,h2,h3,h4,p1,p2', but the format for IPv6 addresses varies
 * depending on the FTP service. Based on not-so-thorough testing, it appears
 * that 'ProFTPD' only returns the last 16 bits of the IPv6 address (4a8f,p1,p2),
 * while 'vsftpd' returns '0,0,0,0,p1,p2'. At least, that's the case when the
 * transfer IP remains the same as the control connection.
 */
static esp_err_t get_transfer_addr(int sockfd, char *ip, char *port)
{
	ssize_t ret;
	char buffer[256] = {0};
	// There could be multiple FTP outputs when connection is lagging
	char *transfer_str = NULL;
	uint8_t i = 0, commas_num = 0;
	uint8_t h1, h2, h3, h4, p1, p2;
	char ipv6[5];

	for (uint8_t i = 0; i < 50; ++i) {
		ret = recv(sockfd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
		if (ret > 0) {
			if ((transfer_str = strstr(buffer, "227 Entering Passive Mode ("))) {
				ESP_LOGI(TAG, "FTP> %s", buffer);
				break;
			} else {
				ESP_LOGE(TAG, "Didn't receive PASV IP/PORT");
				return ESP_FAIL;
			}
		}

		vTaskDelay(pdMS_TO_TICKS(100));
	}

	while (transfer_str[i++] != '(')
		;
	while (transfer_str[i] && transfer_str[i++] != ')') {
		if (',' == transfer_str[i]) {
			++commas_num;
		}
	}

	if (5 == commas_num) {
		ret = sscanf(transfer_str,
			"227 Entering Passive Mode (%hhu,%hhu,%hhu,%hhu,%hhu,%hhu).",
			&h1, &h2, &h3, &h4, &p1, &p2);

	} else if (2 == commas_num) { // 'ProFTPD' IPv6 format: IPv6,p1,p2
		ret = sscanf(transfer_str,
			"227 Entering Passive Mode (%4[^,],%hhu,%hhu)",
			ipv6, &p1, &p2);

	}

	if (6 == ret) {
		if (h1 != 0) {
			sprintf(ip, "%hhu.%hhu.%hhu.%hhu", h1, h2, h3, h4);

		} else { // 'vsftpd' IPv6 format: 0,0,0,0,p1,p2
			sprintf(ip, "%s", g_conn_info.ip);

		}

	} else if (3 == ret) {
		sprintf(ip,
			"%.*s:%s",
			(int)(strlen(g_conn_info.ip) - 5),
			g_conn_info.ip,
			ipv6);

	} else {
		ESP_LOGE(TAG, "Failed to parse PASV IP/PORT");

		return ESP_FAIL;

	}

	sprintf(port, "%hu", p1 << 8 | p2);

	return ESP_OK;
}

static void close_ftp(int sockfd, bool verbose)
{
	ftp_send_command(sockfd, "QUIT", NULL, verbose);

	if (verbose) {
		ftp_receive_response(sockfd);
	}

	close(sockfd);
}

esp_err_t init_ftp_client(const char *host, const char *port, const char *user, const char *pass)
{
	struct addrinfo hints;
	struct addrinfo *result;
	int sockfd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	sockfd = getaddrinfo_tryconnect(&hints, &result, host, port);
	if (-1 == sockfd) {
		freeaddrinfo(result);
		return ESP_FAIL;
	}
	close_ftp(sockfd, false);

	memset(&g_conn_info, 0, sizeof(g_conn_info));

	g_conn_info.ai_family = result->ai_family;
	g_conn_info.ai_socktype = result->ai_socktype;
	g_conn_info.ai_protocol = result->ai_protocol;
	g_conn_info.ai_addrlen = result->ai_addrlen;

	g_conn_info.ai_addr = malloc(result->ai_addrlen);
	if (g_conn_info.ai_addr == NULL) {
		ESP_LOGE(TAG, "g_conn_info.ai_addr: memory allocation failed");
		freeaddrinfo(result);
		return ESP_FAIL;
	}
	memmove(g_conn_info.ai_addr, result->ai_addr, result->ai_addrlen);

	g_conn_info.ip = host;
	g_conn_info.user = user;
	g_conn_info.pass = pass;

	freeaddrinfo(result);

	ESP_LOGI(TAG, "FTP client initialized");

	return ESP_OK;
}

esp_err_t ftp_upload_data(const char *data_path, const uint8_t *data, size_t size)
{
	int sockfd = ftp_connect();
	if (-1 == sockfd) {
		return ESP_ERR_NOT_FOUND;
	}
	ftp_receive_response(sockfd);

	ESP_ERROR_RETURN(ftp_send_command(sockfd, "USER", g_conn_info.user, true));
	ftp_receive_response(sockfd);
	ESP_ERROR_RETURN(ftp_send_command(sockfd, "PASS", g_conn_info.pass, true));
	ftp_receive_response(sockfd);

	ESP_ERROR_RETURN(ftp_send_command(sockfd, "TYPE", "I", true));
	ftp_receive_response(sockfd);

	ESP_ERROR_RETURN(ftp_send_command(sockfd, "PASV", NULL, true));
	char pasv_ip[IP_LEN] = {0};
	char pasv_port[PORT_LEN] = {0};
	ESP_ERROR_RETURN(get_transfer_addr(sockfd, pasv_ip, pasv_port));

	struct addrinfo hints;
	struct addrinfo *result;
	int data_sockfd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = g_conn_info.ai_family;
	hints.ai_socktype = g_conn_info.ai_socktype;
	hints.ai_protocol = g_conn_info.ai_protocol;

	data_sockfd = getaddrinfo_tryconnect(&hints, &result, pasv_ip, pasv_port);
	freeaddrinfo(result);
	if (-1 == data_sockfd) {
		close_ftp(sockfd, true);
		return ESP_FAIL;
	}

	if (ftp_send_command(sockfd, "STOR", data_path, true) != ESP_OK) {
		close(data_sockfd);
		close_ftp(sockfd, true);
		return ESP_FAIL;
	}
	ftp_receive_response(sockfd);

	if (send(data_sockfd, data, size, 0) == -1) {
		ESP_LOGE(TAG, "Failed in sending data");
		close(data_sockfd);
		close_ftp(sockfd, true);
		return ESP_FAIL;
	}

	close(data_sockfd);
	ftp_receive_response(sockfd);

	close_ftp(sockfd, true);

	return ESP_OK;
}
