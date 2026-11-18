# TÀI LIỆU TOÀN DIỆN: GIẢI THÍCH CHI TIẾT DỰ ÁN SECURE GATEWAY AES STM32
> **Dành cho người mới bắt đầu từ con số 0**  
> *Dự án: Cổng chuyển tiếp bảo mật 2 chiều mã hóa AES-128-CBC trên STM32F103C8T6 & Dual W5500*

---

## MỤC LỤC
1. [Dự án này là gì và giải quyết bài toán gì?](#1-dự-án-này-là-gì-và-giải-quyết-bài-toán-gì)
2. [Sơ đồ phần cứng và luồng dữ liệu (Data Pipeline)](#2-sơ-đồ-phần-cứng-và-luồng-dữ-liệu-data-pipeline)
3. [KIỂM TRA LỖI CODE (Cực kỳ quan trọng & Cách khắc phục)](#3-kiểm-tra-lỗi-code-cực-kỳ-quan-trọng--cách-khắc-phục)
4. [Các khái niệm cơ bản cần biết trước khi đọc code](#4-các-khái-niệm-cơ-bản-cần-biết-trước-khi-đọc-code)
5. [Giải thích chi tiết cấu trúc thư mục & từng file trong dự án](#5-giải-thích-chi-tiết-cấu-trúc-thư-mục--từng-file-trong-dự-án)
   - [5.1. Thư mục Config & Common (Cấu hình hệ thống)](#51-thư-mục-config--common-cấu-hình-hệ-thống)
   - [5.2. Thư mục Core (Khởi tạo vi điều khiển)](#52-thư-mục-core-khởi-tạo-vi-điều-khiển)
   - [5.3. Thư mục BSP - Board Support Package (Tầng điều khiển phần cứng)](#53-thư-mục-bsp---board-support-package-tầng-điều-khiển-phần-cứng)
   - [5.4. Thư mục Services (Tầng dịch vụ nghiệp vụ)](#54-thư-mục-services-tầng-dịch-vụ-nghiệp-vụ)
   - [5.5. Thư mục Tasks (Tầng đa nhiệm FreeRTOS)](#55-thư-mục-tasks-tầng-đa-nhiệm-freertos)
   - [5.6. Thư mục App (Điểm kết nối toàn bộ hệ thống)](#56-thư-mục-app-điểm-kết-nối-toàn-bộ-hệ-thống)
6. [Bản đồ tài nguyên bộ nhớ (Flash & RAM)](#6-bản-đồ-tài-nguyên-bộ-nhớ-flash--ram)
7. [Tổng kết và các câu hỏi thường gặp](#7-tổng-kết-và-các-câu-hỏi-thường-gặp)

---

## 1. DỰ ÁN NÀY LÀ GÌ VÀ GIẢI QUYẾT BÀI TOÁN GÌ?

### 1.1. Bối cảnh bài toán thực tế
Giả sử bạn có 2 mạng máy tính độc lập (ví dụ: **Mạng A** - khu vực nội bộ cảm biến/máy móc công nghiệp, và **Mạng B** - khu vực văn phòng hoặc đám mây/máy chủ).
- Nếu nối dây mạng thẳng từ Mạng A sang Mạng B, bất kỳ kẻ tấn công nào nghe lén trên đường truyền đều có thể đọc trộm dữ liệu nhạy cảm (plaintext - văn bản thô).
- Nếu một máy tính bị hack, virus có thể tràn sang toàn bộ mạng còn lại.

### 1.2. Giải pháp của dự án này
Dự án chế tạo một **Cổng bảo mật phần cứng (Hardware Secure Gateway)** bằng chip vi điều khiển **STM32F103C8T6** (thường gọi là bo mạch *Blue Pill*) đóng vai trò là "người gác cổng" nằm ở giữa 2 mạng:
1. Vi điều khiển gắn với **2 chip Ethernet riêng biệt W5500** (Chip A nối Mạng A, Chip B nối Mạng B).
2. Khi Mạng A gửi một gói tin thường (TCP hoặc UDP):
   - Chip W5500 #1 thu nhận gói tin.
   - STM32 lấy dữ liệu ra, tạo một mã ngẫu nhiên IV 16-byte, mã hóa toàn bộ dữ liệu bằng thuật toán mã hóa tối tân **AES-128-CBC**.
   - Đóng gói khung bảo mật: `[Độ dài 2 byte] + [Mã IV 16 byte] + [Dữ liệu đã mã hóa]`.
   - Bắn gói tin đã mã hóa qua Chip W5500 #2 sang Mạng B.
3. Quá trình ngược lại (B gửi sang A) cũng diễn ra y hệt: Mã hóa dữ liệu từ B rồi bắn sang A.
4. Kẻ xấu trên đường cáp mạng chỉ thấy các chuỗi byte ngẫu nhiên vô nghĩa đã được mã hóa AES, không thể nào giải mã được nếu không có khóa bí mật (Secret Key).

---

## 2. SƠ ĐỒ PHẦN CỨNG VÀ LUỒNG DỮ LIỆU (DATA PIPELINE)

### 2.1. Sơ đồ kết nối phần cứng

```
+-------------------+        SPI1 (DMA)        +----------------------+        SPI2 (DMA)        +-------------------+
|    MẠNG LAN A     | <----------------------> |    STM32F103C8T6     | <----------------------> |    MẠNG LAN B     |
| (192.168.10.x)    |   Module W5500 #1        |     (Blue Pill)      |   Module W5500 #2        | (192.168.20.x)    |
| Cáp RJ45          |   CS: PA4, RST: PA3      |   CPU: 72 MHz        |   CS: PB12, RST: PB11    | Cáp RJ45          |
| IP: 192.168.10.2  |   INT: PA2 (EXTI2)       |   RAM: 20 KB         |   INT: PB10 (EXTI15_10)  | IP: 192.168.20.2  |
+-------------------+                          |   FLASH: 64 KB       |                          +-------------------+
                                               +----------------------+
                                                    |            |
                                               UART1 (PA9/10)  LED PC13
                                                    |            |
                                                Máy tính PC    Heartbeat
                                                Debug Log      (Nhịp tim)
```

### 2.2. Luồng dữ liệu qua hệ điều hành thời gian thực (FreeRTOS Data Pipeline)

Hệ thống hoạt động với **5 tác vụ (Tasks)** chạy song song, trao đổi dữ liệu qua **2 Hàng đợi (Queues)** và **1 Bể chứa 10 ngăn tĩnh (Static Buffer Pool)**:

```
[Ngắt W5500 hoặc Hẹn giờ 20ms]
             |
             v
+------------------------+
| Task: RxSpi1 / RxSpi2  |  ---> 1. Lấy 1 ngăn trống từ Bể đệm (relay_service_alloc_slot)
| (Đọc dữ liệu nhận về)  |       2. Đọc dữ liệu thô từ W5500 qua SPI
+------------------------+       3. Gửi chỉ số ngăn (slot_idx: 0..9) vào Hàng đợi RX Queue
             |
             v (RX Queue: chứa số 0..9)
+------------------------+
| Task: PacketProcTask   |  ---> 1. Rút slot_idx từ RX Queue ra
| (Xử lý mã hóa)         |       2. Lấy dữ liệu thô, chèn Padding PKCS#7
+------------------------+       3. Sinh mã IV ngẫu nhiên 16 bytes bằng PRNG
             |                   4. Mã hóa AES-128-CBC
             |                   5. Đóng gói [2B Len][16B IV][Ciphertext]
             |                   6. Gửi slot_idx vào TX Queue
             v (TX Queue: chứa số 0..9)
+------------------------+
| Task: TxTask           |  ---> 1. Rút slot_idx từ TX Queue ra
| (Bắn gói tin đi)       |       2. Gửi gói tin mã hóa sang chip W5500 đối diện qua SPI
+------------------------+       3. Trả lại ngăn đệm cho Bể đệm (relay_service_free_slot)

+------------------------+
| Task: HeartbeatTask    |  ---> Chạy đều đặn mỗi 200ms:
| (Giám sát & Nhịp tim)  |       1. Nhấp nháy LED PC13
+------------------------+       2. "Nuôi chó" (Refresh Watchdog IWDG 500ms) chống treo chip
                                 3. Kiểm tra dây mạng (Link Status) & cứu Socket TCP bị đơ
```

---

## 3. KIỂM TRA LỖI CODE (CỰC KỲ QUAN TRỌNG & CÁCH KHẮC PHỤC)

Sau khi kiểm tra toàn diện mã nguồn trong workspace, phát hiện các vấn đề kỹ thuật sau:

### 🔴 LỖI 1 (LỖI CHÍ MẠNG): Hàm `app_main_init()` không bao giờ được gọi!
- **Hiện tượng**: 
  - Trong `Core/Src/main.c`, dòng 98 gọi `MX_IWDG_Init()` để bật mạch giám sát phần cứng (Independent Watchdog). Watchdog này đếm ngược 500ms, nếu không ai "cho ăn" (kick watchdog), vi điều khiển sẽ tự động Reset toàn bộ.
  - Sau đó gọi `MX_FREERTOS_Init()` và `osKernelStart()`.
  - Trong `Core/Src/freertos.c`, tác vụ `StartDefaultTask` chỉ có một vòng lặp `vTaskDelay(1000)` vô nghĩa.
  - Hàm khởi tạo toàn bộ ứng dụng `app_main_init()` (nằm trong `App/Src/app_main.c`) **KHÔNG ĐƯỢC GỌI Ở BẤT CỨ ĐÂU**!
- **Hậu quả cực kỳ nghiêm trọng**:
  1. Toàn bộ phần cứng W5500, mạng, mã hóa AES và 5 tác vụ FreeRTOS **không bao giờ được chạy**.
  2. Watchdog không có ai nuôi (`HeartbeatTask` không được tạo), do đó **cứ mỗi 500ms vi điều khiển lại bị Reset một lần (Endless Bootloop)**!
- **Cách sửa**:
  Mở file `Core/Src/freertos.c`, tìm hàm `StartDefaultTask` và gọi `app_main_init()` ngay khi RTOS vừa bắt đầu chạy:

```c
/* File: Core/Src/freertos.c */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
    /* GỌI HÀM KHỞI TẠO TOÀN BỘ CỔNG BẢO MẬT TẠI ĐÂY */
    (void)app_main_init();

    /* Sau khi khởi tạo xong các task nghiệp vụ, task mặc định này có thể xóa chính nó
       hoặc ngủ để tiết kiệm CPU */
    const TickType_t idle_delay_ticks = pdMS_TO_TICKS(1000U);
    for (;;)
    {
        vTaskDelay(idle_delay_ticks);
    }
  /* USER CODE END StartDefaultTask */
}
```

---

### 🟠 LỖI 2: Xả tràn gói UDP sai hàm gây kẹt hàng đợi mạng
- **Vị trí**: `Services/Src/relay_service.c`, dòng 228 - 238 trong hàm `relay_service_poll_rx`.
- **Mô tả lỗi**:
  Khi Bể đệm bị đầy (hết 10 slot trống), code cố gắng đọc bỏ bớt dữ liệu để giải phóng bộ nhớ đệm phần cứng của W5500:
  ```c
  uint8_t discard_buf[64];
  const uint16_t drop_len = ...;
  (void)recv(sn, discard_buf, drop_len); // <-- LỖI Ở ĐÂY!
  ```
  Trong thư viện WIZnet (`socket.c`, dòng 657), hàm `recv()` có lệnh kiểm tra `CHECK_SOCKMODE(Sn_MR_TCP)`. Nếu Socket đó là **UDP**, hàm `recv()` lập tức trả về lỗi `-4 (SOCKERR_SOCKMODE)` và **không đọc bất kỳ byte nào**!
- **Hậu quả**: Khi có gói tin UDP đến mà bể đệm đầy, gói tin UDP bị kẹt cứng trong chip W5500 không xả ra được, dẫn đến cờ `rx_avail > 0` liên tục, gây vòng lặp vô tận tiêu tốn CPU.
- **Cách sửa**: Phải kiểm tra giao thức, nếu UDP thì dùng `recvfrom()` để xả:
  ```c
  if (p_cfg->protocol == PROTO_TYPE_TCP_SERVER)
  {
      (void)recv(sn, discard_buf, drop_len);
  }
  else
  {
      uint8_t dummy_ip[4];
      uint16_t dummy_port;
      (void)recvfrom(sn, discard_buf, drop_len, dummy_ip, &dummy_port);
  }
  ```

---

### 🟡 VẤN ĐỀ 3: Ghi đè hàm Callback ngắt EXTI
- **Vị trí**:
  1. Trong `Services/Src/net_service.c` dòng 116: đăng ký `bsp_w5500_register_exti_callback(net_service_on_w5500_exti_isr);` (để đếm số lần ngắt).
  2. Sau đó trong `Tasks/Src/gateway_tasks.c` dòng 283: lại đăng ký `bsp_w5500_register_exti_callback(gateway_on_w5500_exti_from_isr);` (để đánh thức Task).
- **Phân tích**: Biến lưu con trỏ hàm callback `s_exti_callback` trong `bsp_w5500.c` là biến đơn (chỉ lưu 1 hàm). Khi task đăng ký, nó ghi đè mất hàm của `net_service`, khiến hàm `net_service_fetch_exti_count()` luôn trả về 0.
- **Đánh giá**: Không làm chết chương trình chính vì `gateway_tasks` là cái cần nhận ngắt để đánh thức task, nhưng đây là một thiết kế chưa tối ưu.

---

### 🟡 VẤN ĐỀ 4: Đề phòng ngắt SPI DMA Callback
- **Vị trí**: `BSP/Src/bsp_spi.c`.
- **Mô tả**: Code đã cài đặt `HAL_SPI_TxCpltCallback` và `HAL_SPI_TxRxCpltCallback`. Trên STM32F1 ở chế độ Full Duplex Master, khi gọi `HAL_SPI_Receive_DMA`, HAL sẽ gọi `HAL_SPI_TransmitReceive_DMA` và kích hoạt `TxRxCpltCallback`. Tuy nhiên, để an toàn tuyệt đối trước mọi bản cập nhật firmware của ST HAL, nên khai báo bổ sung cả hàm `HAL_SPI_RxCpltCallback` trỏ vào cùng bộ xử lý `bsp_spi_notify_dma_complete_from_isr(hspi, false)`.

---

## 4. CÁC KHÁI NIỆM CƠ BẢN CẦN BIẾT TRƯỚC KHI ĐỌC CODE

Nếu bạn là người mới toanh, hãy nắm 5 khái niệm "vàng" này trong 3 phút:

1. **FreeRTOS là gì?**
   Là một Hệ điều hành thời gian thực siêu nhỏ gọn. Bình thường vi điều khiển chỉ chạy 1 hàm `while(1)` từ trên xuống dưới. FreeRTOS cho phép bạn chia chương trình thành nhiều **Task** (tác vụ) độc lập, đánh lừa cảm giác các task đang chạy song song cùng một lúc.
2. **Queue (Hàng đợi) là gì?**
   Giống như băng chuyền chuyển hàng: Task A bỏ một gói hàng vào đầu băng chuyền, Task B đứng ở cuối băng chuyền lấy ra xử lý. Nhờ Queue mà 2 task không bao giờ tranh giành hay làm hỏng dữ liệu của nhau.
3. **Tại sao dự án dùng "Zero-malloc" (Bộ nhớ tĩnh - Static Allocation)?**
   Lệnh `malloc()` hay `new` (cấp phát động) rất nguy hiểm trong vi điều khiển vì sau vài ngày chạy, bộ nhớ RAM 20KB sẽ bị "phân mảnh" (fragmentation) và làm sập hệ thống (Crash). Dự án này tự tạo sẵn 10 chiếc khay cố định (mỗi khay 550 byte) ngay khi bật nguồn. Cần dùng thì mượn 1 khay, dùng xong thì trả lại. Đảm bảo chạy 10 năm không bao giờ tràn RAM!
4. **DMA (Direct Memory Access) là gì?**
   Bình thường CPU phải tự tay bốc từng byte qua SPI gửi sang chip W5500 (rất tốn thời gian). DMA giống như "nhân viên giao hàng tự động": CPU chỉ cần bảo "chuyển 500 byte này sang W5500", sau đó CPU rảnh tay đi làm việc khác. Khi chuyển xong, DMA sẽ gõ cửa báo CPU.
5. **Mã hóa AES-128-CBC và IV là gì?**
   - **AES-128**: Thuật toán mã hóa chuẩn quốc tế với khóa dài 128 bit (16 bytes).
   - **CBC (Cipher Block Chaining)**: Khối sau bị phụ thuộc vào khối trước, khiến kẻ trộm dù gửi cùng 1 tin nhắn 100 lần thì kết quả mã hóa mỗi lần ra một chuỗi byte hoàn toàn khác nhau.
   - **IV (Initialization Vector - Vector khởi tạo)**: Một dãy 16 byte ngẫu nhiên được sinh ra cho mỗi gói tin. Bên gửi gửi kèm IV này ở đầu gói để bên nhận dùng nó giải mã.

---

## 5. GIẢI THÍCH CHI TIẾT CẤU TRÚC THƯ MỤC & TỪNG FILE TRONG DỰ ÁN

Dự án được phân tầng (Layered Architecture) rất chuẩn mực theo phong cách kỹ sư nhúng chuyên nghiệp:

```
secure_gateway_aes_stm32/
├── Config/     -> Chứa các thông số cấu hình tĩnh (IP, Port, Khóa AES)
├── Common/     -> Mã lỗi chung toàn hệ thống
├── Core/       -> Mã nguồn do STM32CubeIDE tạo ra (Khởi tạo Clock, Ngoại vi)
├── BSP/        -> Board Support Package (Giao tiếp phần cứng trực tiếp)
├── Services/   -> Các khối logic nghiệp vụ (Mã hóa, Mạng, Bộ đệm)
├── Tasks/      -> Các tác vụ FreeRTOS và Hàng đợi
├── Middlewares/-> Thư viện ngoài (FreeRTOS, Tiny AES, Driver W5500)
└── App/        -> Tầng ứng dụng kết nối tất cả các tầng lại với nhau
```

---

### 5.1. Thư mục Config & Common (Cấu hình hệ thống)

#### 📄 `Config/Inc/app_config.h`
File cấu hình kích thước và thời gian chờ (timeout) toàn cục:
- `APP_MAX_PLAINTEXT_SIZE (512U)`: Một gói tin nhận vào tối đa là 512 byte.
- `APP_AES_BLOCK_SIZE (16U)`: Khối xử lý của AES luôn là 16 byte.
- `APP_AES_IV_SIZE (16U)`: Độ dài vector khởi tạo IV là 16 byte.
- `APP_FRAME_LEN_HEADER_SIZE (2U)`: Phần đầu khung ghi độ dài gói tin chiếm 2 byte.
- `APP_STATIC_BUFFER_SIZE (550U)`: Mỗi ngăn đệm có kích thước 550 byte (đủ chứa `2B Len + 16B IV + 512B Data + 16B Padding = 546B`).
- `APP_STATIC_BUFFER_COUNT (10U)`: Tổng cộng có đúng 10 ngăn đệm trong Bể đệm tĩnh.
- `APP_IWDG_TIMEOUT_MS (500U)`: Chó canh phòng Watchdog cắn sau 500ms nếu không nuôi.
- `APP_HEARTBEAT_PERIOD_MS (200U)`: Cứ mỗi 200ms thì nuôi Watchdog và chớp LED 1 lần.

#### 📄 `Config/Inc/crypto_keys.h`
Chứa 2 khóa bí mật AES 128-bit (16 byte):
- `CRYPTO_KEY_A_TO_B_INIT`: Khóa dùng khi mã hóa gói tin từ Mạng A gửi sang Mạng B.
- `CRYPTO_KEY_B_TO_A_INIT`: Khóa dùng khi mã hóa gói tin từ Mạng B gửi sang Mạng A.

#### 📄 `Config/Inc/protocol_config.h`
Bảng cấu hình địa chỉ mạng cho 2 cổng Ethernet W5500:
- **Cổng A (W5500 #1 trên SPI1)**:
  - MAC: `00:08:DC:10:10:01`
  - IP: `192.168.10.2` (Subnet: `255.255.255.0`, Gateway: `192.168.10.1`)
- **Cổng B (W5500 #2 trên SPI2)**:
  - MAC: `00:08:DC:20:20:02`
  - IP: `192.168.20.2` (Subnet: `255.255.255.0`, Gateway: `192.168.20.1`)
- **Cấu hình Socket trên mỗi chip (mỗi chip mở 3 socket)**:
  - Socket 0: TCP Server lắng nghe trên cổng `5000` (Kênh chính).
  - Socket 1: TCP Server lắng nghe trên cổng `5000` (Kênh dự phòng khi Socket 0 bận).
  - Socket 2: UDP lắng nghe trên cổng `5001`.

#### 📄 `Common/Inc/app_status.h`
Định nghĩa mã trả về chuẩn cho toàn bộ dự án (`APP_STATUS_OK`, `APP_STATUS_INVALID_ARG`, `APP_STATUS_BUSY`, `APP_STATUS_TIMEOUT`, `APP_STATUS_NO_MEMORY`, ...). Mọi hàm đều trả về kiểu này để dễ kiểm soát lỗi.

---

### 5.2. Thư mục Core (Khởi tạo vi điều khiển)

- **`main.c`**:
  - `HAL_Init()`: Khởi tạo thư viện HAL của ST.
  - `SystemClock_Config()`: Thiết lập xung nhịp CPU chạy tối đa 72 MHz (sử dụng dao động nội HSI qua bộ nhân tần PLL).
  - Khởi tạo chân GPIO, bộ DMA, các bộ SPI1, SPI2, UART1 và Watchdog IWDG.
  - Gọi `MX_FREERTOS_Init()` và `osKernelStart()` để nhường quyền điều khiển CPU cho FreeRTOS.
- **`freertos.c`**: Tạo tác vụ đầu tiên `StartDefaultTask`.
- **`gpio.c`**: Cấu hình các chân điều khiển (LED, Chân chọn chip CS, Chân ngắt INT của W5500).
- **`spi.c` & `dma.c`**: Cấu hình bộ SPI1 và SPI2 hoạt động cùng DMA1 Channel 2, 3, 4, 5.
- **`iwdg.c`**: Cấu hình bộ đếm Watchdog chống treo chip.
- **`usart.c`**: Cấu hình cổng UART1 với tốc độ 115200 bps để gửi log ra máy tính.

---

### 5.3. Thư mục BSP - Board Support Package (Tầng điều khiển phần cứng)

BSP là tầng "cách ly": Code nghiệp vụ không cần quan tâm thanh ghi STM32 là gì, chỉ cần gọi các hàm thân thiện của BSP.

#### 📄 `bsp_gpio.c` / `.h`
- `bsp_gpio_led_write(bool is_on)`: Bật/tắt LED chân PC13 (lưu ý: LED trên bo Blue Pill hoạt động mức thấp, kéo xuống 0V là sáng, kéo lên 3.3V là tắt).
- `bsp_gpio_led_toggle()`: Đảo trạng thái LED (tạo nhịp tim).
- `bsp_gpio_w5500_cs_write(chip_id, is_selected)`: Kéo chân CS của W5500 xuống LOW để bắt đầu nói chuyện qua SPI, kéo lên HIGH để kết thúc.
- `bsp_gpio_w5500_rst_write(chip_id, is_reset_active)`: Điều khiển chân Reset phần cứng của W5500.

#### 📄 `bsp_spi.c` / `.h`
Điều khiển giao tiếp SPI với W5500:
- `bsp_spi_transfer_byte()`: Gửi/nhận 1 byte (dùng chế độ chờ vòng lặp Polling).
- `bsp_spi_write_burst()` & `bsp_spi_read_burst()`: Gửi/nhận một mảng dữ liệu nhiều byte.
  - Nếu mảng nhỏ (< 16 byte) hoặc RTOS chưa chạy: dùng Polling bình thường.
  - Nếu mảng lớn (>= 16 byte) và RTOS đang chạy: **tự động kích hoạt DMA** và dùng Semaphore tĩnh `s_spi_dma_sem_handle` để cho Task ngủ, giải phóng CPU cho task khác làm việc. Khi DMA bắn xong, ngắt DMA đánh thức task dậy.

#### 📄 `bsp_w5500.c` / `.h`
Cầu nối cực kỳ quan trọng giữa vi điều khiển và 2 chip W5500:
- **Mutex bảo vệ thư viện (`s_wiz_mutex_handle`)**: Vì thư viện chuẩn của hãng WIZnet chỉ thiết kế cho 1 chip, nên khi điều khiển 2 chip, ta phải dùng Mutex để tuần tự hóa. Ai muốn nói chuyện với chip nào thì phải khóa Mutex, chọn chip đó, truyền nhận xong thì nhả Mutex.
- `bsp_w5500_hw_reset()`: Kéo chân RST của W5500 xuống 2ms rồi nhả ra và chờ 50ms để chip W5500 khóa mạch PLL ổn định.
- `bsp_w5500_read_version()`: Đọc thanh ghi `VERSIONR` của chip W5500 (luôn phải trả về giá trị `0x04`).
- `bsp_w5500_is_link_up()`: Đọc thanh ghi `PHYCFGR` để biết dây mạng LAN có đang cắm hay bị rút.
- `HAL_GPIO_EXTI_Callback()`: Khi có gói tin đến, W5500 kéo chân INT xuống, hàm ngắt này sẽ bắt được và gọi callback tương ứng.

#### 📄 `bsp_uart.c` / `.h` & `bsp_watchdog.c` / `.h`
- `bsp_uart_send()`: Gửi chuỗi byte qua UART1 với thời gian chờ an toàn (có kiểm tra con trỏ NULL).
- `bsp_watchdog_refresh()`: Gửi lệnh nạp lại bộ đếm Watchdog (nuôi chó).

---

### 5.4. Thư mục Services (Tầng dịch vụ nghiệp vụ)

#### 📄 `log_service.c` / `.h`
Dịch vụ in nhật ký (Debug Log) an toàn đa nhiệm:
- Sử dụng Mutex `s_log_mutex_handle` để các task khác nhau in log không bao giờ bị đè chữ lên nhau.
- Tự động gắn tiền tố `[RELAY DEBUG]`.
- Kích hoạt bộ đếm chu kỳ phần cứng **ARM Cortex-M3 DWT (Data Watchpoint and Trace)**: Cho phép đo thời gian chạy của hàm chính xác đến từng micro-giây ($\mu s$).

#### 📄 `crypto_service.c` / `.h`
Dịch vụ mã hóa gói tin AES-128-CBC:
- Khởi tạo sẵn cấu trúc mã hóa cho cả 2 chiều: A->B và B->A.
- **Hàm `crypto_generate_iv()`**: Bộ sinh số ngẫu nhiên siêu nhẹ **xorshift32** kết hợp với bộ đếm CPU Tick và DWT Cycle để sinh ra 16 byte IV hoàn toàn ngẫu nhiên cho mỗi gói tin.
- **Hàm `crypto_service_encrypt_frame()`**:
  1. Thêm byte đệm PKCS#7 (ví dụ dữ liệu dài 10 byte thì thêm 6 byte giá trị `0x06` để tròn 16 byte).
  2. Tạo 16 byte IV ngẫu nhiên.
  3. Mã hóa AES-128-CBC.
  4. Đóng gói khung: 2 byte đầu ghi độ dài (Big-Endian) + 16 byte IV + dữ liệu mã hóa.
- **Hàm `crypto_service_run_self_test()`**: Tự kiểm tra thuật toán bằng vector mẫu chuẩn của Viện Tiêu chuẩn và Kỹ thuật Quốc gia Hoa Kỳ (**NIST SP 800-38A**). Đảm bảo mã hóa chính xác 100% trước khi cho hệ thống chạy.
- **Hàm `crypto_service_run_benchmark()`**: Đo và in ra thời gian mã hóa các gói 64B, 128B, 256B, 512B.

#### 📄 `net_service.c` / `.h`
- Cài đặt IP, Subnet, Gateway cho Chip A và Chip B.
- Mở 3 socket (2 TCP Server cổng 5000, 1 UDP cổng 5001) và bật cờ ngắt phần cứng cho từng socket.

#### 📄 `relay_service.c` / `.h`
"Trái tim" của toàn bộ hệ thống chuyển tiếp dữ liệu:
- **Quản lý Bể đệm tĩnh (Static Buffer Pool)**: Có 10 slot `s_buffer_pool[10]`, quản lý bằng mảng trạng thái `s_slot_in_use[10]` và bảo vệ bằng Mutex `s_pool_mutex_handle`.
- `relay_service_alloc_slot()` / `free_slot()`: Lấy một khay trống / Trả khay đã dùng xong.
- `relay_service_poll_rx()`: Kiểm tra xem có dữ liệu đến ở các socket không. Nếu có thì mượn 1 slot, đọc dữ liệu vào, rồi gọi hàm đẩy vào hàng đợi FreeRTOS.
- `relay_service_encrypt_slot()`: Lấy dữ liệu trong slot, gọi tầng crypto để mã hóa đè lên chính slot đó.
- `relay_service_transmit_slot()`: Xác định chip đối diện (nếu nhận từ A thì gửi sang B, nếu nhận từ B thì gửi sang A) và bắn gói tin mã hóa ra mạng.
- `relay_service_supervise_health()`: Bác sĩ gia đình chạy định kỳ:
  - Nếu dây mạng bị rút: Đóng toàn bộ socket để tránh rò rỉ trạng thái.
  - Khi cắm dây lại: Tự động khởi tạo lại socket.
  - Nếu một kết nối TCP bị "treo" (quá 5000ms không gửi nốt dữ liệu): Tự động ngắt kết nối và mở lại cổng để đón client mới.

---

### 5.5. Thư mục Tasks (Tầng đa nhiệm FreeRTOS)

#### 📄 `gateway_tasks.c` / `.h`
Khởi tạo và quản lý toàn bộ 5 tác vụ độc lập:

| Tên Task | Độ ưu tiên | Bộ nhớ Stack | Vai trò chi tiết |
| :--- | :---: | :---: | :--- |
| **`RxSpi1Task`** | 4 (Cao) | 256 words (1024 B) | Đứng đợi tín hiệu ngắt từ Chip W5500 #1 (Network A). Khi có tin đến, đọc vào buffer pool và đẩy slot vào RX Queue. |
| **`RxSpi2Task`** | 4 (Cao) | 256 words (1024 B) | Tương tự RxSpi1 nhưng phục vụ cho Chip W5500 #2 (Network B). |
| **`PacketProcTask`** | 3 (Trung bình) | 320 words (1280 B) | Đợi gói tin trong RX Queue. Rút ra -> Mã hóa AES-128-CBC -> Đẩy vào TX Queue. |
| **`TxTask`** | 3 (Trung bình) | 256 words (1024 B) | Đợi gói tin trong TX Queue. Rút ra -> Bắn qua chip W5500 đối diện -> Giải phóng slot trả lại cho Bể đệm. |
| **`HeartbeatTask`** | 2 (Thấp) | 192 words (768 B) | Chạy mỗi 200ms: Nhấp nháy LED PC13, cho Watchdog ăn (Refresh IWDG), giám sát dây mạng và phục hồi kết nối. |

**Đồng bộ hóa ngắt siêu nhanh (Zero Latency)**:  
Hàm ngắt `gateway_on_w5500_exti_from_isr()` sử dụng hàm FreeRTOS `vTaskNotifyGiveFromISR()` để đánh thức trực tiếp task nhận tương ứng mà không cần phải chờ đợi chu kỳ quét vòng (polling).

---

### 5.6. Thư mục App (Điểm kết nối toàn bộ hệ thống)

#### 📄 `App/Src/app_main.c` & `App/Inc/app_main.h`
Hàm `app_main_init(void)` là "Tổng công trình sư" kết nối toàn bộ các mắt xích:
1. Đặt trạng thái ban đầu cho các chân CS và Reset (HIGH).
2. Khởi tạo `log_service_init()` và in thông tin xung nhịp CPU (`SYSCLK = 72 MHz`).
3. Khởi tạo `bsp_w5500_init()` (Reset cả 2 chip và gán callback SPI).
4. Đọc thanh ghi phiên bản của cả 2 chip W5500 (phải là `0x04`). Nếu không phải `0x04` (do lỏng dây hoặc hỏng chip), hệ thống dừng lại báo lỗi ngay.
5. Cấu hình mạng `net_service_init()`.
6. Khởi tạo mã hóa `crypto_service_init()` và chạy tự kiểm tra NIST `crypto_service_run_self_test()`.
7. Khởi tạo bể đệm `relay_service_init()`.
8. Bắt đầu toàn bộ 5 task và 2 hàng đợi `gateway_tasks_start_all()`.
9. Nuôi watchdog lần đầu `bsp_watchdog_refresh()`.

---

## 6. BẢN ĐỒ TÀI NGUYÊN BỘ NHỚ (FLASH & RAM)

Vi điều khiển **STM32F103C8T6** có tài nguyên rất khiêm tốn: **64 KB Flash** (chứa code) và **20 KB RAM** (chứa biến và stack). Bảng tính toán thực tế cho dự án:

| Thành phần | Dung lượng RAM chiếm dụng | Ghi chú |
| :--- | :---: | :--- |
| **Bể đệm tĩnh (10 slots x 550B)** | 5,500 bytes | Mảng `s_buffer_pool` lưu trữ gói tin trung chuyển |
| **Stack cho 5 Task FreeRTOS** | 5,120 bytes | `(256 + 256 + 320 + 256 + 192) words x 4 bytes` |
| **Khối điều khiển Task (TCB) & Queue** | ~500 bytes | Quản lý tác vụ và hàng đợi tĩnh |
| **FreeRTOS Heap (`configTOTAL_HEAP_SIZE`)** | 3,072 bytes | Dành cho CMSIS-RTOS và các cấu trúc phụ |
| **Bộ đệm Benchmark & Log UART** | ~1,250 bytes | Mảng in log và đo tốc độ AES |
| **Biến toàn cục, HAL Driver, Ngắt** | ~1,500 bytes | Biến của STM32 HAL và WIZnet ioLibrary |
| **TỔNG CỘNG RAM SỬ DỤNG** | **~16.9 KB / 20 KB** | **Chiếm ~84.5% RAM (Rất tối ưu và an toàn, còn dư ~3.1 KB)** |

> [!TIP]
> Nhờ 100% sử dụng **Static Allocation** (cấp phát tĩnh tại thời điểm biên dịch), dung lượng RAM này là **cố định vĩnh viễn**, không bao giờ bị tăng lên khi đang chạy, triệt tiêu hoàn toàn nỗi lo tràn bộ nhớ (Memory Leak) hay phân mảnh RAM.

---

## 7. TỔNG KẾT VÀ CÁC CÂU HỎI THƯỜNG GẶP

### Q1: Sau khi nạp code, làm sao để biết mạch đang hoạt động bình thường?
- **Nhìn LED PC13**: LED nhấp nháy đều đặn mỗi 200ms -> RTOS và `HeartbeatTask` đang sống tốt. Nếu LED tắt ngóm hoặc đứng im -> Hệ thống đã bị treo hoặc bị Watchdog Reset liên tục.
- **Mở cổng COM UART1 (115200-8-N-1)**: Bạn sẽ thấy log khởi động:
  ```
  [RELAY DEBUG] System Boot OK | SYSCLK = 72 MHz
  [RELAY DEBUG] W5500 #1 (SPI1) VERSIONR = 0x04 | W5500 #2 (SPI2) VERSIONR = 0x04
  [RELAY DEBUG] Net Init OK | W5500_A: 192.168.10.2 | W5500_B: 192.168.20.2
  [RELAY DEBUG] [TEST PASS] NIST SP 800-38A AES-128-CBC Vector Verified!
  [RELAY DEBUG] FreeRTOS 5 Static Tasks & Buffer Pool (10x550B) Started!
  ```

### Q2: Tại sao phải sửa Lỗi 1 trước tiên?
Nếu không gọi `app_main_init()` trong `StartDefaultTask`, mạch sẽ không in bất cứ dòng log nào ra UART, LED không nháy và chip sẽ tự Reset liên tục mỗi nửa giây do Watchdog.

### Q3: Muốn đổi IP mạng thì sửa ở đâu?
Mở file `Config/Inc/protocol_config.h`, sửa các hằng số:
- Mạng A: `PROTO_NET_A_IP_0..3`
- Mạng B: `PROTO_NET_B_IP_0..3`
- Cổng TCP/UDP: `PROTO_TCP_RELAY_PORT` và `PROTO_UDP_RELAY_PORT`.

---
*Tài liệu được biên soạn tự động với phân tích mã nguồn chi tiết, chuyên sâu và dễ hiểu nhất.*
