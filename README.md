# 📂⚡ **Speedy Disposable File Server** ⚡📂  
> A lightweight, disposable file transfer server for blazing-fast file sharing without encryption or the hassle of installing services. 🚀

---

## 🛠️ **Project Overview**  

Have you ever needed to quickly transfer a file over a network without encryption overhead, service installations, or a steep learning curve? Look no further! **Speedy Disposable File Server** is here:  
- 🖥️ **Server**: A simple, disposable TCP server.  
- 💻 **Client**: Sends files directly to the server.  
- ⚡ **Fast**: Prioritizes speed over encryption.  
- 🗂️ **Incremental**: Ideal for transferring large files.  
- 📉 **Compression**: Compresses data for minimal network usage.  

---

## ✨ **Features**  

- 🚀 **Super Fast**: Skip encryption and transfer files at maximum speed.  
- 🧰 **Minimal Setup**: No services to install. Just run and go!  
- 📦 **Block-based**: Only sends necessary chunks for incremental updates.  
- 📉 **Compression**: Uses `zlib` to save bandwidth.  
- 🛑 **Disposable Server**: Perfect for temporary use cases—fire it up, transfer your file, and shut it down.  

---

## 🎯 **Use Cases**  

- 📤 Quickly transferring a single file to another machine on the same network.  
- ⚙️ Temporary, on-the-fly file sharing during development or debugging.  
- 🖥️ Bootstrapping environments without file-sharing tools preinstalled.  

---

## 🚀 **Getting Started**  

### 1️⃣ **Requirements**  
Before you start, ensure you have:  
- 🐧 **Linux** or 🪟 **Windows** with GCC (or another compiler).  
- 🛠️ `zlib` installed for compression.  

### 2️⃣ **Setup**  

Clone this repository:  
```bash
git clone https://github.com/yourusername/speedy-disposable-file-server.git
cd speedy-disposable-file-server
```

### 3️⃣ **Compile**  

#### For the server:  
```bash
make -f Makefile.server
```

#### For the client:  
```bash
make -f Makefile.client
```

---

## 🌟 **How to Use**  

### **Step 1**: Start the Server  
Run the server to listen for incoming file transfers:  
```bash
./server
```
The server will listen on `127.0.0.1:12345` by default.  

### **Step 2**: Send a File  
Use the client to send a file to the server:  
```bash
./client
```

🎉 Voilà! Your file will be sent to the server and saved as `output_file`.  

---

## 🔍 **Detailed Example**  

### File Transfer Example  
1. On Machine A (Server):  
   ```bash
   ./server
   ```
   Output:  
   ```
   Server listening on port 12345...
   Client connected.
   File received and written to output_file
   ```

2. On Machine B (Client):  
   ```bash
   ./client
   ```
   Output:  
   ```
   Connected to server.
   File sent successfully.
   ```

---

## 🤔 **Why No Encryption?**  

Encryption is great for security but can slow down file transfers and increase complexity. This project is built for **trusted local environments** (like within the same LAN), where speed and simplicity are top priorities.  

🔒 **Note**: If you need secure file transfers, this tool isn’t for you. Consider tools like `scp` or `rsync` with SSH instead.  

---

## 🔧 **Technical Details**  

### **Protocol Overview**  
- 🧱 **Block-based Transfer**: Files are divided into 4 KB chunks.  
- 🔍 **Compression**: Each block is compressed using `zlib`.  
- 📤 **Client Sends**: Block size and compressed block data.  
- 📥 **Server Writes**: Decompresses and writes blocks to a file.  

---

## 🐛 **Known Limitations**  

- 🚫 **No Encryption**: Transfers are in plain text. Not secure for untrusted networks.  
- 🛠️ **Single File**: Only supports transferring one file per session.  
- 🌐 **Local Use**: Designed for use in trusted local networks.  

---

## 🎨 **Future Improvements**  

- 🔄 **Add Multi-File Support**: Extend the protocol for directory transfers.  
- 📜 **Logging**: Include logs for file transfer progress.  
- 🌐 **Custom IPs/Ports**: Allow specifying IP/port from command line.  
- ⚡ **Parallel Transfers**: Optimize for concurrent file uploads.  

---

## 👷‍♂️ **Contributing**  

Want to make this project better? 🛠️  
1. Fork the repository.  
2. Create a feature branch.  
3. Submit a pull request.  

---

## 💬 **Feedback & Support**  

Found a bug or need help? Open an issue in the repository or reach out to us via 📧 email.  

---

## 📝 **License**  

This project is licensed under the MIT License. Do whatever you want with it, but please include attribution. 😊  

---

### 🌟 **Enjoy transferring files with ease and speed!** 🌟