#!/usr/bin/env python3
"""
Bộ công cụ kiểm thử tự động cho dự án Secure Gateway (STM32F103 + Dual W5500 + AES-128-CBC).
Khớp với khóa mật mã trong Config/Inc/crypto_keys.h và định dạng khung [len(2B)][IV(16B)][ciphertext].
"""

import random
import socket
import struct
import time
import zlib
from Crypto.Cipher import AES

# ==============================================================================
# 1. CẤU HÌNH MẠNG VÀ KHÓA MẬT MÃ (PHẢI KHỚP 100% VỚI FIRMWARE STM32)
# ==============================================================================
PC_NET_A_IP = "192.168.10.10"
W5500_A_IP  = "192.168.10.2"

PC_NET_B_IP = "192.168.20.10"
W5500_B_IP  = "192.168.20.2"

TCP_PORT = 5000
UDP_PORT = 5001

# Khóa 128-bit khớp với CRYPTO_KEY_A_TO_B_INIT và CRYPTO_KEY_B_TO_A_INIT
KEY_A_TO_B = bytes([
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
])

KEY_B_TO_A = bytes([
    0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
    0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81
])

MAX_PLAINTEXT_SIZE = 512


# ==============================================================================
# 2. CÁC HÀM XỬ LÝ KHUNG TIN, ĐỆM PKCS#7, CRC32 VÀ GIẢI MÃ AES-128-CBC
# ==============================================================================
def pkcs7_unpad(data: bytes) -> bytes:
    """Gỡ đệm PKCS#7 và kiểm tra tính hợp lệ của các byte đệm."""
    if len(data) == 0 or (len(data) % 16) != 0:
        raise ValueError("Do dai ban giai ma khong phai boi so cua 16 byte")
    pad_len = data[-1]
    if pad_len < 1 or pad_len > 16:
        raise ValueError(f"Gia tri dem PKCS#7 khong hop le: {pad_len}")
    if data[-pad_len:] != bytes([pad_len]) * pad_len:
        raise ValueError("Cac byte dem PKCS#7 khong dong nhat")
    return data[:-pad_len]


def build_test_payload(seq_num: int, total_size: int = 512, tag: bytes = b"SECURE_GW") -> bytes:
    """
    Đóng gói bản rõ kiểm thử gồm: [seq_num (4B)] + [nội dung ASCII] + [CRC32 (4B)].
    Giúp bên nhận phát hiện ngay nếu giải mã sai dù chỉ 1 bit.
    """
    if total_size < 16 or total_size > MAX_PLAINTEXT_SIZE:
        raise ValueError("Kich thuoc payload phai nam trong khoang 16..512 byte")
    header = struct.pack(">I", seq_num)
    body_len = total_size - 8
    pattern = tag + f"_SEQ_{seq_num:05d}_".encode("ascii")
    repeats = (body_len // len(pattern)) + 1
    body = (pattern * repeats)[:body_len]
    raw_without_crc = header + body
    crc_val = zlib.crc32(raw_without_crc) & 0xFFFFFFFF
    return raw_without_crc + struct.pack(">I", crc_val)


def verify_and_extract_payload(plaintext: bytes):
    """Kiểm tra CRC32 của bản rõ sau khi giải mã và trả về (is_valid, seq_num, preview_text)."""
    if len(plaintext) < 8:
        return False, -1, ""
    raw_without_crc = plaintext[:-4]
    expected_crc = struct.unpack(">I", plaintext[-4:])[0]
    actual_crc = zlib.crc32(raw_without_crc) & 0xFFFFFFFF
    seq_num = struct.unpack(">I", raw_without_crc[:4])[0]
    preview = raw_without_crc[4:36].decode("ascii", errors="replace")
    return (expected_crc == actual_crc), seq_num, preview


def decrypt_gateway_frame(frame_body: bytes, key: bytes) -> bytes:
    """
    Giải mã phần thân khung [IV (16B)][ciphertext] nhận được từ W5500 đích.
    """
    if len(frame_body) < 32 or ((len(frame_body) - 16) % 16) != 0:
        raise ValueError(f"Kich thuoc khung ma hoa khong hop le: {len(frame_body)}B")
    iv = frame_body[:16]
    ciphertext = frame_body[16:]
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    decrypted_padded = cipher.decrypt(ciphertext)
    return pkcs7_unpad(decrypted_padded)


def tcp_recv_exact(sock: socket.socket, num_bytes: int) -> bytes:
    """Đọc chính xác num_bytes từ luồng TCP (tránh lỗi phân mảnh gói tin TCP)."""
    chunks = bytearray()
    while len(chunks) < num_bytes:
        part = sock.recv(num_bytes - len(chunks))
        if not part:
            raise ConnectionError("Ket noi TCP bi ngat khi dang doc khung")
        chunks.extend(part)
    return bytes(chunks)


def tcp_recv_gateway_frame(sock: socket.socket) -> tuple[bytes, bytes]:
    """Đọc trọn vẹn 1 khung [len_hi][len_lo][IV(16B)][ciphertext] từ TCP."""
    len_hdr = tcp_recv_exact(sock, 2)
    payload_len = struct.unpack(">H", len_hdr)[0]
    frame_body = tcp_recv_exact(sock, payload_len)
    iv = frame_body[:16]
    return iv, frame_body


def open_tcp_pair():
    """Mở cặp kết nối TCP tới W5500 #1 (Mạng A) và W5500 #2 (Mạng B)."""
    sock_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_a.settimeout(3.0)
    sock_a.bind((PC_NET_A_IP, 0))
    sock_a.connect((W5500_A_IP, TCP_PORT))

    sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_b.settimeout(3.0)
    sock_b.bind((PC_NET_B_IP, 0))
    sock_b.connect((W5500_B_IP, TCP_PORT))
    return sock_a, sock_b


# ==============================================================================
# 3. BỐN KỊCH BẢN KIỂM THỬ THỰC NGHIỆM (KHỚP VỚI MỤC V TRONG BÀI BÁO)
# ==============================================================================
def test_mode_1_e2e_bidirectional():
    print("\n--- [MODE 1] KIEM THU GIAI MA DAU-CUOI HAI CHIEU (TCP & UDP) ---")
    num_packets = 50
    sock_a, sock_b = open_tcp_pair()

    try:
        # 1. Chiều TCP A -> B
        ok_a2b = 0
        seen_ivs = set()
        for seq in range(1, num_packets + 1):
            plain_tx = build_test_payload(seq, total_size=128, tag=b"MSG_A2B")
            sock_a.sendall(plain_tx)
            iv, frame_body = tcp_recv_gateway_frame(sock_b)
            seen_ivs.add(iv)
            plain_rx = decrypt_gateway_frame(frame_body, KEY_A_TO_B)
            valid, rx_seq, preview = verify_and_extract_payload(plain_rx)
            if valid and plain_rx == plain_tx:
                ok_a2b += 1
            if seq <= 3:
                print(f"  [TCP A->B #{seq}] IV={iv.hex()[:16]}... | Giai ma: '{preview}' | CRC={'OK' if valid else 'FAIL'}")

        print(f"=> Ket qua TCP A -> B: Thanh cong {ok_a2b}/{num_packets} goi | So IV duy nhat: {len(seen_ivs)}/{num_packets}")

        # 2. Chiều TCP B -> A
        ok_b2a = 0
        for seq in range(1, num_packets + 1):
            plain_tx = build_test_payload(seq, total_size=128, tag=b"MSG_B2A")
            sock_b.sendall(plain_tx)
            iv, frame_body = tcp_recv_gateway_frame(sock_a)
            plain_rx = decrypt_gateway_frame(frame_body, KEY_B_TO_A)
            valid, rx_seq, preview = verify_and_extract_payload(plain_rx)
            if valid and plain_rx == plain_tx:
                ok_b2a += 1
            if seq <= 3:
                print(f"  [TCP B->A #{seq}] IV={iv.hex()[:16]}... | Giai ma: '{preview}' | CRC={'OK' if valid else 'FAIL'}")

        print(f"=> Ket qua TCP B -> A: Thanh cong {ok_b2a}/{num_packets} goi")
    finally:
        sock_a.close()
        sock_b.close()

    # 3. Chiều UDP A -> B
    udp_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_a.settimeout(2.0)
    udp_b.settimeout(2.0)
    try:
        udp_a.bind((PC_NET_A_IP, UDP_PORT))
        udp_b.bind((PC_NET_B_IP, UDP_PORT))
        ok_udp = 0
        for seq in range(1, 31):
            plain_tx = build_test_payload(seq, total_size=128, tag=b"UDP_A2B")
            udp_a.sendto(plain_tx, (W5500_A_IP, UDP_PORT))
            datagram, _ = udp_b.recvfrom(1024)
            payload_len = struct.unpack(">H", datagram[:2])[0]
            frame_body = datagram[2:2 + payload_len]
            plain_rx = decrypt_gateway_frame(frame_body, KEY_A_TO_B)
            valid, _, _ = verify_and_extract_payload(plain_rx)
            if valid and plain_rx == plain_tx:
                ok_udp += 1
            time.sleep(0.02)
        print(f"=> Ket qua UDP A -> B: Thanh cong {ok_udp}/30 goi")
    finally:
        udp_a.close()
        udp_b.close()


def test_mode_2_throughput():
    print("\n--- [MODE 2] DO THONG LUONG THUC TE TCP & UDP (PAYLOAD 512B, MUC TIEU 50 GOI/GIAY) ---")
    duration_sec = 10.0
    target_pps = 50.0
    interval = 1.0 / target_pps

    # Đo thông lượng TCP
    sock_a, sock_b = open_tcp_pair()
    try:
        sent_count = 0
        recv_count = 0
        crc_ok_count = 0
        start_t = time.perf_counter()
        next_send_t = start_t

        while (time.perf_counter() - start_t) < duration_sec:
            now = time.perf_counter()
            if now >= next_send_t:
                pkt = build_test_payload(sent_count + 1, total_size=512)
                sock_a.sendall(pkt)
                sent_count += 1
                next_send_t += interval

                _, frame_body = tcp_recv_gateway_frame(sock_b)
                plain_rx = decrypt_gateway_frame(frame_body, KEY_A_TO_B)
                valid, _, _ = verify_and_extract_payload(plain_rx)
                recv_count += 1
                if valid:
                    crc_ok_count += 1

        elapsed = time.perf_counter() - start_t
        pps = recv_count / elapsed
        kbps = (recv_count * 512.0) / 1024.0 / elapsed
        loss_pct = max(0.0, (sent_count - recv_count) * 100.0 / max(1, sent_count))
        print(f"[TCP 512B] Gui: {sent_count} | Nhan: {recv_count} (CRC OK: {crc_ok_count}) "
              f"| Thong luong: {pps:.2f} goi/giay ({kbps:.2f} KB/s) | Mat goi: {loss_pct:.1f}%")
    finally:
        sock_a.close()
        sock_b.close()

    time.sleep(0.5)

    # Đo thông lượng UDP
    udp_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_b.settimeout(0.005)
    try:
        udp_a.bind((PC_NET_A_IP, UDP_PORT))
        udp_b.bind((PC_NET_B_IP, UDP_PORT))
        sent_udp = 0
        recv_udp = 0
        crc_ok_udp = 0
        start_t = time.perf_counter()
        next_send_t = start_t

        while (time.perf_counter() - start_t) < duration_sec:
            now = time.perf_counter()
            if now >= next_send_t:
                pkt = build_test_payload(sent_udp + 1, total_size=512)
                udp_a.sendto(pkt, (W5500_A_IP, UDP_PORT))
                sent_udp += 1
                next_send_t += interval

            try:
                datagram, _ = udp_b.recvfrom(1024)
                payload_len = struct.unpack(">H", datagram[:2])[0]
                plain_rx = decrypt_gateway_frame(datagram[2:2 + payload_len], KEY_A_TO_B)
                valid, _, _ = verify_and_extract_payload(plain_rx)
                recv_udp += 1
                if valid:
                    crc_ok_udp += 1
            except socket.timeout:
                pass

        elapsed = time.perf_counter() - start_t
        pps = recv_udp / elapsed
        kbps = (recv_udp * 512.0) / 1024.0 / elapsed
        loss_pct = max(0.0, (sent_udp - recv_udp) * 100.0 / max(1, sent_udp))
        print(f"[UDP 512B] Gui: {sent_udp} | Nhan: {recv_udp} (CRC OK: {crc_ok_udp}) "
              f"| Thong luong: {pps:.2f} goi/giay ({kbps:.2f} KB/s) | Mat goi: {loss_pct:.1f}%")
    finally:
        udp_a.close()
        udp_b.close()


def test_mode_3_reconnect_recovery():
    print("\n--- [MODE 3] DO THOI GIAN KHOI PHUC KET NOI TCP (SOCKET DU PHONG, n=20 LAN) ---")
    trials = 20
    recovery_times_ms = []

    sock_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_b.settimeout(3.0)
    sock_b.bind((PC_NET_B_IP, 0))
    sock_b.connect((W5500_B_IP, TCP_PORT))

    try:
        for idx in range(1, trials + 1):
            sock_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock_a.settimeout(3.0)
            sock_a.bind((PC_NET_A_IP, 0))
            sock_a.connect((W5500_A_IP, TCP_PORT))

            # Gửi 1 gói xác nhận kết nối hoạt động
            pkt = build_test_payload(idx, total_size=64)
            sock_a.sendall(pkt)
            _, frame_body = tcp_recv_gateway_frame(sock_b)
            _ = decrypt_gateway_frame(frame_body, KEY_A_TO_B)

            # Ngắt kết nối đột ngột và đo thời gian kết nối lại + truyền gói kế tiếp thành công
            t0 = time.perf_counter()
            sock_a.close()

            sock_a_new = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock_a_new.settimeout(3.0)
            sock_a_new.bind((PC_NET_A_IP, 0))
            sock_a_new.connect((W5500_A_IP, TCP_PORT))

            pkt2 = build_test_payload(idx + 100, total_size=64)
            sock_a_new.sendall(pkt2)
            _, frame_body2 = tcp_recv_gateway_frame(sock_b)
            plain_rx2 = decrypt_gateway_frame(frame_body2, KEY_A_TO_B)
            valid, _, _ = verify_and_extract_payload(plain_rx2)
            dt_ms = (time.perf_counter() - t0) * 1000.0
            sock_a_new.close()

            if valid:
                recovery_times_ms.append(dt_ms)
            time.sleep(0.25)  # Cho heartbeat_task (200ms) dọn dẹp socket cũ xong

        if recovery_times_ms:
            avg_ms = sum(recovery_times_ms) / len(recovery_times_ms)
            print(f"=> Thanh cong {len(recovery_times_ms)}/{trials} lan thu:")
            print(f"   Nho nhat (Min): {min(recovery_times_ms):.2f} ms | "
                  f"Trung binh (TB): {avg_ms:.2f} ms | "
                  f"Lon nhat (Max): {max(recovery_times_ms):.2f} ms")
    finally:
        sock_b.close()


def test_mode_4_frame_loss_comparison():
    print("\n--- [MODE 4] SO SANH IV THEO KHUNG VS IV THEO PHIEN KHI MAT GOI 5% - 50% ---")
    total_frames = 100
    loss_rates = [0.05, 0.10, 0.20, 0.30, 0.40, 0.50]
    rng = random.Random(2026)

    # Thu thập 100 khung thực tế được mã hóa từ phần cứng STM32
    print("Dang thu thap 100 khung ma hoa thuc te tu phan cung STM32...")
    sock_a, sock_b = open_tcp_pair()
    captured_plain = []
    captured_per_frame_cipher = []
    try:
        for seq in range(1, total_frames + 1):
            pt = build_test_payload(seq, total_size=512)
            sock_a.sendall(pt)
            _, frame_body = tcp_recv_gateway_frame(sock_b)
            captured_plain.append(pt)
            captured_per_frame_cipher.append(frame_body)
    finally:
        sock_a.close()
        sock_b.close()

    # Tạo luồng bản mã tương đương theo chế độ "IV nối chuỗi theo phiên" để đối chiếu
    session_iv = b"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10"
    session_cipher_blocks = []
    chained_iv = session_iv
    for pt in captured_plain:
        padded = pt + (b"\x10" * 16)
        enc = AES.new(KEY_A_TO_B, AES.MODE_CBC, iv=chained_iv).encrypt(padded)
        session_cipher_blocks.append(enc)
        chained_iv = enc[-16:]  # Khối bản mã cuối làm IV cho khung kế tiếp

    print("\nTy le mat (%) | IV theo khung: Loi giai ma | IV theo phien: Loi giai ma")
    print("-" * 72)
    for rate in loss_rates:
        per_frame_errors = 0
        session_errors = 0
        rx_chained_iv = session_iv

        for idx in range(total_frames):
            is_dropped = (rng.random() < rate)
            if is_dropped:
                # Khung bị mất trên đường truyền -> bên nhận không nhận được
                continue

            # 1. Giải mã bằng IV theo từng khung (từ STM32)
            try:
                pt_dec = decrypt_gateway_frame(captured_per_frame_cipher[idx], KEY_A_TO_B)
                valid, _, _ = verify_and_extract_payload(pt_dec)
                if not valid:
                    per_frame_errors += 1
            except Exception:
                per_frame_errors += 1

            # 2. Giải mã bằng IV nối chuỗi theo phiên
            enc_blk = session_cipher_blocks[idx]
            dec_sess = AES.new(KEY_A_TO_B, AES.MODE_CBC, iv=rx_chained_iv).decrypt(enc_blk)
            rx_chained_iv = enc_blk[-16:]
            try:
                pt_sess = pkcs7_unpad(dec_sess)
                valid_sess, _, _ = verify_and_extract_payload(pt_sess)
                if not valid_sess:
                    session_errors += 1
            except Exception:
                session_errors += 1

        print(f"    {int(rate * 100):2d}%       |            {per_frame_errors:3d}             |            {session_errors:3d}")


def main():
    while True:
        print("\n============================================================")
        print("  SECURE GATEWAY TEST SUITE (STM32F103 + W5500 + AES-128)")
        print("============================================================")
        print("  1. Kiem thu giai ma E2E hai chieu (TCP & UDP)")
        print("  2. Do thong luong TCP & UDP (512B @ 50 goi/giay)")
        print("  3. Do thoi gian khoi phuc ket noi TCP (Socket du phong)")
        print("  4. Mo phong mat khung 5%-50%: IV theo khung vs IV theo phien")
        print("  0. Thoat")
        choice = input("Nhap lua chon cua ban (0-4): ").strip()

        if choice == "1":
            test_mode_1_e2e_bidirectional()
        elif choice == "2":
            test_mode_2_throughput()
        elif choice == "3":
            test_mode_3_reconnect_recovery()
        elif choice == "4":
            test_mode_4_frame_loss_comparison()
        elif choice == "0":
            print("Ket thuc kiem thu.")
            break
        else:
            print("Lua chon khong hop le, vui long nhap 0, 1, 2, 3 hoac 4.")


if __name__ == "__main__":
    main()