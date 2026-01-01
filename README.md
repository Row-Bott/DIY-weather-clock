# DIY-weather-clock

A DIY ESP‑01S weather clock with OLED display, Wi‑Fi setup portal, location selection, and user‑friendly reset options

esp01s DIY weather clock that ca be purchased for low cost but has Chinese firmware on it that forces you to sign up to their sites. I have written this code to have no sign up whatsoever. 

At the time of writing (30 Dec '25), it requires a slight tweak for resetting the Wi-Fi.

I have written some major UK cities and local ones for my kids. 

https://github.com/Row-Bott/DIY-weather-clock/blob/main/images/DIY%20Weather%20Clock_1.png

Markdown
![Weather Clock] [https://github.com/Row-Bott/DIY-weather-clock/blob/main/images/DIY%20Weather%20Clock_2.png](https://github.com/Row-Bott/DIY-weather-clock/blob/main/images/DIY%20Weather%20Clock_2.png)


********************************
*Flashing the ESP01S           *
********************************

My methdod was using a spare RPi (Zero 2W) and SSH into it. If required I will add the details how I did this at a later date. 

As the ESP01S uses the same 3.3V the RPI Wi-Fi uses it draws more current than the board can handle and drops out. This fix for this is to use the AMS117S 3.3V power on the weather clock board. This way it stops the current draw issues.

When flashing 

***Upload pictures to show***.
