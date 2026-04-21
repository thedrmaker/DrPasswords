# Dr. Passwords 🔐

<p align="center">
  <img src="assets/device.jpg" width="300"/>
</p>

<p align="center">
  <b>A physical open-source password manager built with ESP32-S3</b>
</p>

---

## 🎬 Watch the video

[![Watch the video](https://img.youtube.com/vi/tpA8L6P6DDg/0.jpg)](https://www.youtube.com/watch?v=tpA8L6P6DDg)

---

## 🚀 Features

- 🔐 Store passwords locally (no cloud)
- ⌨️ Type passwords automatically (HID keyboard emulation)
- 👤 Username / Password / Username+TAB+Password modes
- 🔢 Built-in TOTP (2FA) generator
- 📟 On-device UI (screen + buttons)
- ⚙️ Settings page (add / edit / delete passwords)
- 🔑 Generate strong random passwords
- 🌐 Wi-Fi assisted time sync for TOTP

---

## 🧠 Concept

This project is inspired by **hardware crypto wallets**.

Instead of trusting cloud services, your passwords are stored in a **physical device** that you control.

---

## 🛠️ Hardware

### Main board
- ESP32-S3 (recommended)

### Optional parts
- RTC module (for offline time keeping)
- Compact USB adapter (for flash-drive style form factor)

---

### 🔗 Components & Links

| Component | Link |
|----------|------|
| LILYGO ESP32-S3 with 3D case | [Buy here](https://ar.aliexpress.com/item/1005004496543314.html) |
| LILYGO ESP32-S3 | [Buy here](https://s.click.aliexpress.com/e/_c3U8wX7j) |
| RTC Module | [Buy here](https://s.click.aliexpress.com/e/_c4W6CP4H) |
| USB-C to USB-C Adapter | [Buy here](https://s.click.aliexpress.com/e/_c4nUhoEV) |
| USB-C to USB-A Adapter | [Buy here](https://s.click.aliexpress.com/e/_c43FLfM9) |

---

> 💡 Some links may be affiliate links that help support the project.

---

## ⚡ How it works

### 🔐 Password Mode
- Select account
- Device types:
  - username
  - password
  - both automatically

### 🔢 TOTP Mode
- Generates 2FA codes (like Google Authenticator)
- Works based on time + secret key
- Can auto-type the code via USB

### ⚙️ Settings Mode
- Connect to device Wi-Fi
- Open local page
- Add / edit / delete accounts
- Generate passwords

---

## 📦 Status

**Version 1 (Prototype)**

More features coming soon 👇

---

## 🔮 Roadmap

- [ ] Bluetooth HID support
- [ ] Encrypted storage
- [ ] Backup & restore
- [ ] Fingerprint / local unlock
- [ ] Better UI/UX
- [ ] Hardware improvements

---

## ⚠️ Important Notice

This project is for **educational and personal use only**.

❌ Selling this project is NOT allowed  
❌ Commercial use is NOT allowed  

✅ Only the original creator (**Dr. Maker**) has the right to commercialize this project.

---

## ❤️ Support the project

If you like this project and want to support its development:

[![Support me](https://img.shields.io/badge/Support-PayPal-blue?style=for-the-badge&logo=paypal)](https://paypal.com/ncp/payment/CRE2CT9J2JJRQ)


---

## 🤝 Contributing

This is an open-source project — let’s build it together!

You can help by:
- improving the code
- suggesting features
- improving UI/UX
- enhancing security
- testing on different devices

---

## 👨‍💻 Author

**Dr. Maker**

---

## 📄 License

This project uses a custom non-commercial license.  
Commercial use and selling are not allowed.

See the [LICENSE](LICENSE) file for details.
