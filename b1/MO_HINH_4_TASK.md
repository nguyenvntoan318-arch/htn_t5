# B1 - STM32F103C8T6 FreeRTOS

## 1. Mô hình hệ thống

Dự án gồm 4 task:
- LED1_Task: điều khiển độ sáng LED1.
- LED2_Task: điều khiển độ sáng LED2.
- LED3_Task: điều khiển độ sáng LED3.
- Button_Task: đọc nút nhấn và điều chỉnh độ sáng.

## 2. Giao tiếp giữa các task

Button_Task gửi giá trị độ sáng qua 3 Queue riêng biệt.
Mỗi task LED nhận dữ liệu từ Queue tương ứng.

## 3. Chân phần cứng

- LED1: PA0 / TIM2_CH1.
- LED2: PA1 / TIM2_CH2.
- LED3: PA2 / TIM2_CH3.
- SELECT: PB12.
- UP: PB13.
- DOWN: PB15.

## 4. Thông số

- FreeRTOS: 4 task.
- Button_Task: Priority 3.
- Các task LED: Priority 2.
- Tần số PWM: 1 kHz.
- Chu kỳ đọc nút: 10 ms.

