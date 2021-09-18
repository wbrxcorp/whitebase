#include <unistd.h>

#include <vector>
#include <string>
#include <fstream>
#include <memory>
#include <filesystem>

#include <curl/curl.h>
#include <argparse/argparse.hpp>
#include <yajl/yajl_tree.h>

#include "common.h"
#include "crypt.h"

static const std::filesystem::path privkey_path("/etc/walbrix/privkey"), wireguard_dir("/etc/wireguard");

static std::shared_ptr<uint8_t[]> genkey()
{
    std::shared_ptr<uint8_t[]> privkey_bytes(new uint8_t[WG_KEY_LEN]);
    if (getentropy(privkey_bytes.get(), WG_KEY_LEN) != 0) throw std::runtime_error("getentropy() failed");
    // https://github.com/torvalds/linux/blob/master/include/crypto/curve25519.h#L61
    privkey_bytes[0] &= 248;
    privkey_bytes[31] = (privkey_bytes[31] & 127) | 64;
    return privkey_bytes;
}

int wg_genkey(const std::vector<std::string>& args)
{
    argparse::ArgumentParser program(args[0]);
    program.add_argument("--force", "-f").help("Overwrite even if exists").default_value(false).implicit_value(true);

    try {
        program.parse_args(args);
    }
    catch (const std::runtime_error& err) {
        std::cout << err.what() << std::endl;
        std::cout << program;
        return 1;
    }

    bool force = program.get<bool>("--force");
    if (std::filesystem::exists(privkey_path) && !force) {
        std::cout << "Key already exists. Use --force to overwrite" << std::endl;
        return 0;
    }

    if (privkey_path.has_parent_path()) {
        std::filesystem::create_directories(privkey_path.parent_path());
    }

    {
        std::ofstream f(privkey_path);
        if (!f) throw std::runtime_error(privkey_path.string() + " couldn't be created");
        //else
        f << base64_encode(genkey().get(), WG_KEY_LEN);
    }

    return 0;
}

static std::string get_privkey_b64()
{
    std::string privkey_b64;
    {
        std::ifstream f(privkey_path);
        if (!f) throw std::runtime_error("Unable to open " + privkey_path.string());
        // else
        f >> privkey_b64;
    }
    return privkey_b64;
}

static std::shared_ptr<EVP_PKEY> get_privkey(const std::string& privkey_b64)
{
    auto privkey_bytes = base64_decode(privkey_b64);
    if (privkey_bytes.second < WG_KEY_LEN) throw std::runtime_error(privkey_path.string() + " contains invalid private key");
    //else

    std::shared_ptr<EVP_PKEY> privkey(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, privkey_bytes.first.get(), WG_KEY_LEN), EVP_PKEY_free);
    if (!privkey) throw std::runtime_error("EVP_PKEY_new_raw_private_key() failed");
    //else
    return privkey;
}

static std::shared_ptr<uint8_t[]> get_pubkey_bytes(EVP_PKEY* privkey)
{
    std::shared_ptr<uint8_t[]> pubkey_bytes(new uint8_t[WG_KEY_LEN]);
    size_t pubkey_len = WG_KEY_LEN;
    if (!EVP_PKEY_get_raw_public_key(privkey, pubkey_bytes.get(), &pubkey_len)) {
        throw std::runtime_error("EVP_PKEY_get_raw_public_key() failed");
    }
    //else
    return pubkey_bytes;
}

static std::shared_ptr<EVP_PKEY> get_pubkey(const uint8_t* pubkey_bytes)
{
    std::shared_ptr<EVP_PKEY> pubkey(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, pubkey_bytes, WG_KEY_LEN), EVP_PKEY_free);
    if (!pubkey) throw std::runtime_error("Invalid public key");
    return pubkey;
}

int wg_pubkey(const std::vector<std::string>&)
{
    auto privkey = get_privkey(get_privkey_b64());
    auto pubkey_bytes = get_pubkey_bytes(privkey.get());

    std::cout << base64_encode(pubkey_bytes.get(), WG_KEY_LEN) << std::endl;

    return 0;
}

static size_t curl_callback(char *buffer, size_t size, size_t nmemb, void *f)
{
    (*((std::string*)f)) += std::string(buffer, size * nmemb);
    return size * nmemb;
}

static std::string strip_name_from_ssh_key(const std::string& ssh_key)
{
    auto first_spc = ssh_key.find_first_of(' ');
    if (first_spc == ssh_key.npos) throw std::runtime_error("No delimiter found in ssh key");
    auto last_spc = ssh_key.find_first_of(' ', first_spc + 1);
    return last_spc != ssh_key.npos? ssh_key.substr(0, last_spc) : ssh_key;
}

int wg_getconfig(bool accept_ssh_key)
{
    auto privkey_b64 = get_privkey_b64();
    auto privkey = get_privkey(privkey_b64);
    auto pubkey_bytes = get_pubkey_bytes(privkey.get());
    std::string url = "https://hub.walbrix.net/authorized/" + make_urlsafe(base64_encode(pubkey_bytes.get(), WG_KEY_LEN));
    //std::cout << url << std::endl;

    std::shared_ptr<CURL> curl(curl_easy_init(), curl_easy_cleanup);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    std::string buf;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &buf);
    auto res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        throw std::runtime_error(curl_easy_strerror(res));
    }
    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code == 404) {
        std::cerr << "Not authorized yet" << std::endl;
        return 1;
    }
    if (http_code != 200) throw std::runtime_error("Server error: status code=" + std::to_string(http_code));

    auto comma_pos = buf.find_first_of(',');
    if (comma_pos == buf.npos) throw std::runtime_error("Invalid server response: no delimiter");

    auto peer_pubkey_b64 = buf.substr(0, comma_pos);
    buf.erase(buf.begin(), buf.begin() + comma_pos + 1);

    auto peer_pubkey = get_pubkey(base64_decode(peer_pubkey_b64).first.get());

    auto decrypted = decrypt(buf, privkey.get(), peer_pubkey.get());

    char errorbuf[1024];

    std::shared_ptr<yajl_val_s> tree(yajl_tree_parse(decrypted.c_str(), errorbuf, sizeof(errorbuf)), yajl_tree_free);
    if (!tree) throw std::runtime_error(errorbuf);
    if (!YAJL_IS_OBJECT(tree)) throw std::runtime_error("Invalid JSON data");
    std::optional<std::string> address, endpoint, peer_address, ssh_key, serial;
    auto obj = YAJL_GET_OBJECT(tree);
    for (int i = 0; i < obj->len; i++) {
        std::string key(obj->keys[i]);
        auto value = obj->values[i];
        if (key == "address") {
            address = YAJL_GET_STRING(value);
        } else if (key == "endpoint") {
            endpoint = YAJL_GET_STRING(value);
        } else if (key == "peer-address") {
            peer_address = YAJL_GET_STRING(value);
        } else if (key == "ssh-key") {
            ssh_key = YAJL_GET_STRING(value);
        } else if (key == "serial") {
            serial = YAJL_GET_STRING(value);
        }
    }
    if (!address) throw std::runtime_error("Field 'address' is missing");
    if (!endpoint) throw std::runtime_error("Field 'endpoint' is missing");
    if (!peer_address) throw std::runtime_error("Field 'peer-address' is missing");

    std::filesystem::create_directories(wireguard_dir);
    {
        auto conf = wireguard_dir / "wg-walbrix.conf";
        std::ofstream f(conf);
        if (!f) throw std::runtime_error("Failed to open " + conf.string() + " for write");
        f << "[Interface]" << std::endl;
        f << "PrivateKey=" << privkey_b64 << std::endl;
        f << "Address=" << address.value() << std::endl;
        f << "[Peer]" << std::endl;
        f << "PublicKey=" << peer_pubkey_b64 << std::endl;
        f << "endpoint=" << endpoint.value() << std::endl;
        f << "AllowedIPs=" << peer_address.value() << std::endl;
        f << "PersistentKeepalive=25" << std::endl;
    }

    if (serial) {
        check_call({"hostnamectl", "hostname", serial.value()});
    }

    if (ssh_key && accept_ssh_key) {
        const char* HOME = getenv("HOME");
        if (!HOME) HOME = "/root";

        std::string ssh_key_strippted = strip_name_from_ssh_key(ssh_key.value());
        std::filesystem::path home(HOME);
        auto ssh_dir = home / ".ssh";
        if (!std::filesystem::exists(ssh_dir)) {
            std::filesystem::create_directory(ssh_dir);
            std::filesystem::permissions(ssh_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
        }
        auto authorized_keys = ssh_dir / "authorized_keys";
        bool exists = false;
        if (std::filesystem::exists(authorized_keys) && std::filesystem::is_regular_file(authorized_keys)) {

            std::ifstream f(authorized_keys);
            if (f) {
                std::string line;
                while(std::getline(f, line)) {
                    if (ssh_key_strippted == strip_name_from_ssh_key(line)) {
                        exists = true;
                        break;
                    }
                }
            }
        }
        if (!exists) {
            std::ofstream f(authorized_keys, std::ios_base::app);
            if (!f) throw std::runtime_error("Failed to open " + authorized_keys.string() + " for append");
            //else
            f << ssh_key.value() << std::endl;
        }
    }

    return 0;
}

int wg_getconfig(const std::vector<std::string>& args)
{
    argparse::ArgumentParser program(args[0]);
    program.add_argument("--accept-ssh-key", "-k").help("Accept SSH public key").default_value(false).implicit_value(true);

    try {
        program.parse_args(args);
    }
    catch (const std::runtime_error& err) {
        std::cout << err.what() << std::endl;
        std::cout << program;
        return 1;
    }

    return wg_getconfig(program.get<bool>("--accept-ssh-key"));
}

static int _main(int,char*[])
{
    return wg_getconfig(true);
}

#ifdef __MAIN_MODULE__
int main(int argc, char* argv[]) { return _main(argc, argv); }
#endif
