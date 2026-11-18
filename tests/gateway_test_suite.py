#!/usr/bin/env python3
"""
Automated Test Suite for Secure Gateway Project (STM32F103 + Dual W5500 + AES-128-CBC).
Matches cryptographic keys in Config/Inc/crypto_keys.h and frame format [len(2B)][IV(16B)][ciphertext].
"""

import random
import socket
import struct
import time
import zlib
from Crypto.Cipher import AES

# ==============================================================================
# 1. NETWORK CONFIGURATION AND CRYPTOGRAPHIC KEYS (MUST MATCH STM32 FIRMWARE)
# ==============================================================================
PC_NET_A_IP = "192.168.10.10"
W5500_A_IP  = "192.168.10.2"

PC_NET_B_IP = "192.168.20.10"
W5500_B_IP  = "192.168.20.2"

TCP_PORT = 5000
UDP_PORT = 5001

# 128-bit symmetric keys matching CRYPTO_KEY_A_TO_B_INIT and CRYPTO_KEY_B_TO_A_INIT
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
# 2. FRAME PROCESSING, PKCS#7 PADDING, CRC32, AND AES-128-CBC DECRYPTION
# ==============================================================================
def pkcs7_unpad(data: bytes) -> bytes:
    """Remove PKCS#7 padding and validate padding bytes."""
    if len(data) == 0 or (len(data) % 16) != 0:
        raise ValueError("Decrypted length is not a multiple of 16 bytes")
    pad_len = data[-1]
    if pad_len < 1 or pad_len > 16:
        raise ValueError(f"Invalid PKCS#7 padding value: {pad_len}")
    if data[-pad_len:] != bytes([pad_len]) * pad_len:
        raise ValueError("PKCS#7 padding bytes are not uniform")
    return data[:-pad_len]


def build_test_payload(seq_num: int, total_size: int = 512, tag: bytes = b"SECURE_GW") -> bytes:
    """
    Construct test plaintext payload: [seq_num (4B)] + [ASCII content] + [CRC32 (4B)].
    Enables receiver to immediately detect even a single-bit decryption or transmission error.
    """
    if total_size < 16 or total_size > MAX_PLAINTEXT_SIZE:
        raise ValueError("Payload size must be between 16 and 512 bytes")
    header = struct.pack(">I", seq_num)
    body_len = total_size - 8
    pattern = tag + f"_SEQ_{seq_num:05d}_".encode("ascii")
    repeats = (body_len // len(pattern)) + 1
    body = (pattern * repeats)[:body_len]
    raw_without_crc = header + body
    crc_val = zlib.crc32(raw_without_crc) & 0xFFFFFFFF
    return raw_without_crc + struct.pack(">I", crc_val)


def verify_and_extract_payload(plaintext: bytes):
    """Verify CRC32 of decrypted plaintext and return (is_valid, seq_num, preview_text)."""
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
    Decrypt frame body [IV (16B)][ciphertext] received from destination W5500.
    """
    if len(frame_body) < 32 or ((len(frame_body) - 16) % 16) != 0:
        raise ValueError(f"Invalid encrypted frame size: {len(frame_body)}B")
    iv = frame_body[:16]
    ciphertext = frame_body[16:]
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    decrypted_padded = cipher.decrypt(ciphertext)
    return pkcs7_unpad(decrypted_padded)


def tcp_recv_exact(sock: socket.socket, num_bytes: int) -> bytes:
    """Read exact number of bytes from TCP stream (avoids TCP packet fragmentation issues)."""
    chunks = bytearray()
    while len(chunks) < num_bytes:
        part = sock.recv(num_bytes - len(chunks))
        if not part:
            raise ConnectionError("TCP connection closed while reading frame")
        chunks.extend(part)
    return bytes(chunks)


def tcp_recv_gateway_frame(sock: socket.socket) -> tuple[bytes, bytes]:
    """Read complete [len_hi][len_lo][IV(16B)][ciphertext] frame from TCP stream."""
    len_hdr = tcp_recv_exact(sock, 2)
    payload_len = struct.unpack(">H", len_hdr)[0]
    frame_body = tcp_recv_exact(sock, payload_len)
    iv = frame_body[:16]
    return iv, frame_body


def open_tcp_pair():
    """Establish concurrent TCP connections to W5500 #1 (Network A) and W5500 #2 (Network B)."""
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
# 3. EMPIRICAL TEST SCENARIOS (MATCHING SECTION V OF PAPER / DOCUMENTATION)
# ==============================================================================
def test_mode_1_e2e_bidirectional():
    print("\n--- [MODE 1] END-TO-END BIDIRECTIONAL DECRYPTION TEST (TCP & UDP) ---")
    num_packets = 50
    sock_a, sock_b = open_tcp_pair()

    try:
        # 1. TCP Direction A -> B
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
                print(f"  [TCP A->B #{seq}] IV={iv.hex()[:16]}... | Decrypted: '{preview}' | CRC={'OK' if valid else 'FAIL'}")

        print(f"=> TCP A -> B Result: Success {ok_a2b}/{num_packets} packets | Unique IVs: {len(seen_ivs)}/{num_packets}")

        # 2. TCP Direction B -> A
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
                print(f"  [TCP B->A #{seq}] IV={iv.hex()[:16]}... | Decrypted: '{preview}' | CRC={'OK' if valid else 'FAIL'}")

        print(f"=> TCP B -> A Result: Success {ok_b2a}/{num_packets} packets")
    finally:
        sock_a.close()
        sock_b.close()

    # 3. UDP Direction A -> B
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
        print(f"=> UDP A -> B Result: Success {ok_udp}/30 packets")
    finally:
        udp_a.close()
        udp_b.close()


def test_mode_2_throughput():
    print("\n--- [MODE 2] EMPIRICAL TCP & UDP THROUGHPUT (512B PAYLOAD, TARGET 50 PPS) ---")
    duration_sec = 10.0
    target_pps = 50.0
    interval = 1.0 / target_pps

    # Measure TCP throughput
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
        print(f"[TCP 512B] Sent: {sent_count} | Recv: {recv_count} (CRC OK: {crc_ok_count}) "
              f"| Throughput: {pps:.2f} pps ({kbps:.2f} KB/s) | Packet loss: {loss_pct:.1f}%")
    finally:
        sock_a.close()
        sock_b.close()

    time.sleep(0.5)

    # Measure UDP throughput
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
        print(f"[UDP 512B] Sent: {sent_udp} | Recv: {recv_udp} (CRC OK: {crc_ok_udp}) "
              f"| Throughput: {pps:.2f} pps ({kbps:.2f} KB/s) | Packet loss: {loss_pct:.1f}%")
    finally:
        udp_a.close()
        udp_b.close()


def test_mode_3_reconnect_recovery():
    print("\n--- [MODE 3] TCP RECONNECTION RECOVERY TIME (BACKUP SOCKET, n=20 TRIALS) ---")
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

            # Send 1 packet to confirm active connection
            pkt = build_test_payload(idx, total_size=64)
            sock_a.sendall(pkt)
            _, frame_body = tcp_recv_gateway_frame(sock_b)
            _ = decrypt_gateway_frame(frame_body, KEY_A_TO_B)

            # Abruptly close connection and measure reconnect + next packet transmission time
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
            time.sleep(0.25)  # Allow heartbeat_task (200ms) to clean up old socket state

        if recovery_times_ms:
            avg_ms = sum(recovery_times_ms) / len(recovery_times_ms)
            print(f"=> Success {len(recovery_times_ms)}/{trials} trials:")
            print(f"   Min: {min(recovery_times_ms):.2f} ms | "
                  f"Avg: {avg_ms:.2f} ms | "
                  f"Max: {max(recovery_times_ms):.2f} ms")
    finally:
        sock_b.close()


def test_mode_4_frame_loss_comparison():
    print("\n--- [MODE 4] COMPARISON: PER-FRAME IV VS SESSION-CHAINED IV UNDER 5% - 50% LOSS ---")
    total_frames = 100
    loss_rates = [0.05, 0.10, 0.20, 0.30, 0.40, 0.50]
    rng = random.Random(2026)

    # Collect 100 empirical frames encrypted by STM32 hardware
    print("Collecting 100 actual encrypted frames from STM32 hardware...")
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

    # Generate equivalent ciphertext stream using 'session-chained IV' mode for comparison
    session_iv = b"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10"
    session_cipher_blocks = []
    chained_iv = session_iv
    for pt in captured_plain:
        padded = pt + (b"\x10" * 16)
        enc = AES.new(KEY_A_TO_B, AES.MODE_CBC, iv=chained_iv).encrypt(padded)
        session_cipher_blocks.append(enc)
        chained_iv = enc[-16:]  # Last ciphertext block acts as IV for next frame

    print("\nLoss Rate (%) | Per-Frame IV: Decrypt Errors | Session-Chained IV: Decrypt Errors")
    print("-" * 72)
    for rate in loss_rates:
        per_frame_errors = 0
        session_errors = 0
        rx_chained_iv = session_iv

        for idx in range(total_frames):
            is_dropped = (rng.random() < rate)
            if is_dropped:
                # Frame dropped on link -> receiver does not receive it
                continue

            # 1. Decrypt using per-frame IV (from STM32)
            try:
                pt_dec = decrypt_gateway_frame(captured_per_frame_cipher[idx], KEY_A_TO_B)
                valid, _, _ = verify_and_extract_payload(pt_dec)
                if not valid:
                    per_frame_errors += 1
            except Exception:
                per_frame_errors += 1

            # 2. Decrypt using session-chained IV
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
        print("  1. Bidirectional E2E Decryption Test (TCP & UDP)")
        print("  2. Measure Throughput TCP & UDP (512B @ 50 pps)")
        print("  3. Measure TCP Reconnection Recovery Time (Backup Socket)")
        print("  4. Simulate Frame Loss 5%-50%: Per-Frame IV vs Session-Chained IV")
        print("  0. Exit")
        choice = input("Enter your choice (0-4): ").strip()

        if choice == "1":
            test_mode_1_e2e_bidirectional()
        elif choice == "2":
            test_mode_2_throughput()
        elif choice == "3":
            test_mode_3_reconnect_recovery()
        elif choice == "4":
            test_mode_4_frame_loss_comparison()
        elif choice == "0":
            print("Test suite terminated.")
            break
        else:
            print("Invalid choice, please enter 0, 1, 2, 3, or 4.")


if __name__ == "__main__":
    main()
