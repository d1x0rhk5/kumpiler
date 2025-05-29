wget -qO - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" | sudo tee /etc/apt/sources.list.d/llvm18.list
sudo apt update
sudo apt install clang-18 libclang-18-dev llvm-18

pip uninstall clang
pip install clang==18.1.8
