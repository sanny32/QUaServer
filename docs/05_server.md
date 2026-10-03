# Server

Besides a custom port (see the [Basics](01_basics.md) section), the `QUaServer` class allows to set an SSL **certificate** so that clients can **validate** the server, and to customise the server **description** published through OPC UA. All these settings must be applied **before** starting the server.

See the [validation document](validation.md) for more details on how validation works.

## Create Certificates

Make sure the `openssl` command line tool is available and follow the next commands to create the certificate. On Windows, use the `openssl.exe` of the OpenSSL toolkit shipped with Qt (`<Qt root>/Tools/OpenSSLv3/Win_x64/bin`) or the one included in *Git Bash*; on Linux just use the command line.

The first step is to create a Certificate Authority (CA). The CA will take the role of a system integrator commissioned with installing OPC Servers in a plant. The CA will have to:

* Create its own *public* and *private* key pair.

* Create its own **self-signed** *certificate*.

* Create its own *Certificate Revocation List (CRL)*.

Keys can be created and transformed into various formats. Ultimately, most OPC UA applications make use of the [DER format](https://wiki.openssl.org/index.php/DER). 

```bash
# Create directory to store CA's files
mkdir ca
# Create CA key
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out ca/ca.key
# Create self-signed CA cert
openssl req -new -x509 -days 3600 -key ca/ca.key -subj "/CN=juangburgos CA/O=juangburgos Organization" \
-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" -out ca/ca.crt
# Convert cert to der format
openssl x509 -in ca/ca.crt -inform pem -out ca/ca.crt.der -outform der
# Create cert revocation list CRL file
# NOTE : might need to create in relative path
#        - File './demoCA/index.txt' (Empty)
#        - File './demoCA/crlnumber' with contents '1000'
openssl ca -crldays 3600 -keyfile ca/ca.key -cert ca/ca.crt -gencrl -out ca/ca.crl
# Convert CRL to der format
openssl crl -in ca/ca.crl -inform pem -out ca/ca.der.crl -outform der
```

The next steps must be applied for each server the system integrator wants to install.

* Create its own *public* and *private* key pair.

* Create an `exts.txt` which contain the *certificate extensions* required by the OPC UA standard.

* Create its own **unsigned** certificate, and with it a *certificate sign request*.

* Give the *certificate sign request* to the CA to sign it.

The `exts.txt` should be as follows:

```
[v3_ca]
subjectAltName=DNS:localhost,DNS:ppic09,IP:127.0.0.1,IP:192.168.1.18,URI:urn:unconfigured:application
basicConstraints=CA:TRUE
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid,issuer
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth,clientAuth,codeSigning
```

The `subjectAltName` must contains all the URLs that will be used to connect to the server. In the example above, clients might connect to the localhost (`127.0.0.1`) or through the Windows network, using the Windows PC name (`ppic09`), or through the local network (`192.168.1.18`).

```bash
# Create directory to store server's files
mkdir server
# Create server key
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out server/server.key
# Convert server key to der format
openssl rsa -in server/server.key -inform pem -out server/server.key.der -outform der
# Create server cert sign request
openssl req -new -sha256 \
-key server/server.key \
-subj "/C=ES/ST=MAD/O=MyServer/CN=localhost" \
-out server/server.csr
```

The CA must now sign the server's *certificate sign request* to create the *signed certificate*, appending also the required *certificate extensions* (`exts.txt`).

```bash
# Sign cert sign request (NOTE: must provide exts.txt)
openssl x509 -days 3600 -req \
-in server/server.csr \
-extensions v3_ca \
-extfile server/exts.txt \
-CAcreateserial -CA ca/ca.crt -CAkey ca/ca.key \
-out server/server.crt
# Convert cert to der format
openssl x509 -in server/server.crt -inform pem -out server/server.crt.der -outform der
```

## Use Certificates

First the CA's certificate and CRL must be copied to the client's software. 

In the case of *UA Expert*, in the user interface go to `Settings -> Manage Certificates...`. Then click the `Open Certificate Location`, which opens the file explorer to a location similar to:

```
$SOME_PATH/unifiedautomation/uaexpert/PKI/trusted/certs
```

The CA's certificate must be copied to this path:

```bash
cp ca/ca.crt.der $SOME_PATH/unifiedautomation/uaexpert/PKI/trusted/certs/ca.crt.der
```

Going one directory up, then in `crl` is where the CRL must be copied to:

```bash
cp ca/ca.der.crl $SOME_PATH/unifiedautomation/uaexpert/PKI/trusted/crl/ca.der.crl
```

Now the *server certificate* must be copied next to the *QUaServer* application:

```bash
cp server/server.crt.der $SERVER_PATH/server.crt.der
```

And in the C++ code the server's certificate contents need to be passed to the `setCertificate` method **before** starting the server:

```c++
#include <QCoreApplication>
#include <QDebug>
#include <QFile>

#include <QUaServer>

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

	QUaServer server;

	// Load server certificate
	QFile certServer;
	certServer.setFileName("server.crt.der");
	if (!certServer.open(QIODevice::ReadOnly))
	{
		qWarning() << "Could not open" << certServer.fileName();
		return -1;
	}
	server.setCertificate(certServer.readAll());
	certServer.close();

	server.start();

	return a.exec(); 
}
```

Now the client is able to validate the server before connecting to it.

Note that even though validation requires creating and managing cryptographic keys, the communications are yet **not encrypted**. The files generated in this section are used in the [Encryption](07_encryption.md) section to actually encrypt communications.

## Server Description

The `QUaServer` instance also contains methods to add custom server description:

```c++
// Add server description
server.setApplicationName ("my_app");
server.setApplicationUri  ("urn:juangburgos:my_app");
server.setProductName     ("my_product");
server.setProductUri      ("juangburgos.com");
server.setManufacturerName("My Company Inc.");
server.setSoftwareVersion ("6.6.6-master");
server.setBuildNumber     ("gvfsed43fs");
```

These methods should be called **before** starting the server, else the changes won't be visible until the server is restarted. When encryption is used, the `ApplicationUri` must match the `URI` entry in the `subjectAltName` of the server certificate, otherwise the server fails to start.

This information is then made available to the clients through the *Server Object* that can be found by browsing to `/Root/Objects/Server/ServerStatus/BuildInfo`.

<p align="center">
  <img src="../res/img/05_server_01.jpg">
</p>

## Hostname

By default the server listens on all network interfaces. To listen on a single interface, set its hostname or IP address before starting the server:

```c++
server.setHostname("192.168.1.18");
```

The server then also advertises `opc.tcp://<hostname>:<port>` as its discovery URL. If the hostname cannot be resolved, `start()` fails. The hostname must be listed in the `subjectAltName` of the server certificate. Clients that connect through another name (e.g. behind NAT) still get endpoints with the URL they used.

## Server Example

Build and run the [05_server](../examples/05_server/main.cpp) example to learn more.

Some test certificates are included for convenience in [examples/05_server/ca_files](../examples/05_server/ca_files). **Do not use them in production**, just for testing purposes. That CA predates the `keyUsage` extension above, so it cannot be used to trust clients (see [Encryption](07_encryption.md)).
