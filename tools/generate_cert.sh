#!/bin/bash

# Generate a self-signed certificate and private key
# -x509: Output a X.509 structure instead of a cert request
# -newkey rsa:4096: Generate a new RSA key of size 4096 bits
# -keyout key.pem: Where to save the private key
# -out cert.pem: Where to save the certificate
# -sha256: Use SHA-256 digest
# -days 3650: Validity of the certificate
# -nodes: Don't encrypt the private key
# -subj: Set the subject name to avoid interactive prompts

openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -sha256 -days 3650 -nodes -subj "/C=US/ST=State/L=City/O=Organization/OU=Unit/CN=localhost"

echo "Generated cert.pem and key.pem"
