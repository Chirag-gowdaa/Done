#!/bin/bash
set -e

PKG_NAME="wiper"
PKG_VERSION="1.0"
PKG_ARCH="amd64"
BUILD_DIR="${PKG_NAME}_${PKG_VERSION}"

# Clean old build
rm -rf "$BUILD_DIR" *.deb

# Create folder structure
mkdir -p $BUILD_DIR/DEBIAN
mkdir -p $BUILD_DIR/opt/wiper
mkdir -p $BUILD_DIR/lib/systemd/system

# Copy backend, frontend build, and node runtime
cp -r backend $BUILD_DIR/opt/wiper/
cp -r frontend/dist $BUILD_DIR/opt/wiper/frontend
cp -r node $BUILD_DIR/opt/wiper/

# ===== Control file =====
cat <<EOF > $BUILD_DIR/DEBIAN/control
Package: $PKG_NAME
Version: $PKG_VERSION
Section: utils
Priority: optional
Architecture: $PKG_ARCH
Depends: libc6
Maintainer: Chirag <you@example.com>
Description: Secure data wiping and factory reset service with web frontend
EOF

# ===== Post-install script =====
cat <<'EOF' > $BUILD_DIR/DEBIAN/postinst
#!/bin/bash
set -e

# Create dedicated user if not exists
if ! id "wiper" &>/dev/null; then
    useradd -r -s /bin/false wiper
fi

# Install Node dependencies for backend
cd /opt/wiper/backend
/opt/wiper/node/bin/npm install

# Reload systemd and enable service
systemctl daemon-reload
systemctl enable wiper.service
systemctl restart wiper.service

exit 0

EOF
chmod 755 $BUILD_DIR/DEBIAN/postinst

# ===== Systemd service file =====
cat <<EOF > $BUILD_DIR/lib/systemd/system/wiper.service
[Unit]
Description=Wiper Service
After=network.target

[Service]
Environment=NODE_PATH=/opt/wiper/node/bin
ExecStart=/opt/wiper/node/bin/node /opt/wiper/backend/server.js
WorkingDirectory=/opt/wiper/backend
Restart=always
User=wiper

[Install]
WantedBy=multi-user.target
EOF

# Build deb package
dpkg-deb --build $BUILD_DIR
