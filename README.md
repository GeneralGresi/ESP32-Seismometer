This is a DIY Project for building a simple 3D Printable Seismometer, interfacing to InfluxDB/Grafana via an ESP32.


The main principal is a magnet on a spring. Each "movement" will cause the magnet to swing, therefore inducing a voltage/current in a coil. 

![Screenshot_20240114_125006](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/cd93e05b-07b3-4128-9e7b-05ccd3b6f8b3)
![Schematic Seismometer Stand](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/27e75eee-aaa6-452f-9ed5-6ced874864e9)



As the coil itself would not produce much voltage, we have to amplify this voltage. Depending on the strength of a potential earthquake I was unsure which gain is needed.
Therefore I disigned a multi gain op-amp circuit utilizing high precision LN1677 chips. This implements a gain of 10, 100 and 1000. Each gain is funneled seperately to an Analog Input pin on the ESP32.

![Screenshot_20240114_125238](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/fd34ab64-4644-4c27-886a-37b8c35ffdac)

The op-amp circuit works as follows: Via a voltage devider we create a median voltage between 0 and 3.3V. The coil is connected directly to this voltage devider, as well as to each inverting input of the op amps.
The difference between the median voltage and the input voltage is then amplified by the respective gain, creating a bigger offset value.

![Circuit](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/5a6a370e-0d6a-4c4b-bb92-c86acfefb0c3)


The ESP32 reads these values, funnels it through a software low-pass filter of 35 Hz (Earth Quakes are usually between 0.1Hz and 30Hz, I also want to filter out the mains voltage of 50Hz, therefore everything below 35Hz is considered acceptable).
As due to unknown reasons (at least for me), we are not perfectly centered to 0 during no movement of the magnet, therefore we take the average of 500 readings (at a sample rate of about 200Hz/~5000ys delay between reading we have an average of 2,5 Seconds). 
This average then is used to move the raw median value to 0. This approach works pretty well.

I mapped the min and max values from -1000 to 1000 for each input. If we overshoot this value at for example the X1000 value, we use the next-lowest beeing X100, and multiply it by 10, to get consistent values to InfluxDB.
A value of 300 on the X100 Gain Input will be transmited as 3000 to InfluxDB. The same applies for X10/X100.
The final value then get's put into a queue, as well as the current timestamp. 

I use NTP for time synchronisation - we can't use the server timestamp, as we never send data right away.
The values are first put into a Queue and send the to InfluxDB in batches by a seperate thread.
This ensured, the wifi send command doesn't interrupt further reading of new values. 

For winding of the coil, there is a separate script for an Arduino Uno (or whatever microcontroller), which uses magnets and a magnet switch to register each rotation and display it on an LCD. Serial Printing the value would also be an option though.
The magnet switch is connected to a cordless drill, the Base is connected via a Metric Screw, and a 3D printed adapter. Use about 300-500 windings. More windings = Increased sensitivity.
![CoilWinding](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/dd5082c9-e3e3-471e-b041-6a73591047f1)


The magnetic part of the seismometer looks as follows: It uses 15mm and 10mm Magnets, as well as a 5mm steel threaded rod and some fixation rings with setscrews.
![Magnet Part](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/f65d08d3-4cd2-42f8-8dea-b6decfd934a5)
![Magnet on Spring](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/a7f24d49-f968-47a1-8d60-3a9a929b0d85)

The upper two magnets are inducing the voltage into the coil, the bottom two are for dampening the movement. Therefore a copper tube with 20mm (inner 18mm) in diameter is inserted into the base.
![Base Prototype](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/5728d9f4-c5e3-44a0-a136-9f8c44466a6b)

Due to eddy currents, the copper tube acts like a dampener/break for the magnets, slowing them down. This greatly shortens swinging after an event.
Using the 3D printed Setscrew, the contraption is lowered into the base, making sure the bigger magnets are perfectly centered within the coil, and the dampening magnets are reaching the copper tube. 
They should be at least 1cm within the tube, in order to have the dampening work even when a swinging motion has the contraption in it's highest position.
This can take some time fiddling around, testing longer or shorter steel rods, and cutting the spring to length. I can't give you any exact numbers, as this depends greatly on the weight of each component.

![Magnet Base](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/271bfca2-2745-4433-bf88-38d30c47f6ce)


Once finished it should look like that:
![Magnet Base 2](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/025b8ff4-1134-4586-a654-778ded762ea8)

On the Edges there are M16 Screws and nuts to level the Seismometer. For increased sensitivity, steel threaded Rods which are hammered into the ground can be used.  
For leveling, take a look into each side hole of the base, and make sure the magent contraption is perfectly centered. If it is off, or even worse, it rubs against the copper tube, the reading will be absolutely garbage.

Once you are finished, carefully slíde over and scrow on the conver over the base. This prevents air movement to cause any distortion.

Regarding the circuit:
I enhoused it in a plastic box, with a buck converter which is creating a steady 5V, a Li-Ion module to serve as UPS, as well as some additional capacitors and a choke to smooth the input voltage even more.
![Screenshot_20240114_133038](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/ddc9fcd1-87c8-47bc-b76c-1f0b409033e8)
![Circuit Box](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/2210d0f0-00ac-4511-bb79-22189d3d276e)




End result:
![IMG_20240114_133147](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/40b43faf-3be7-4ed6-8c35-4219ab64a2f0)

Why the aluminium foil one may ask? I don't think it helps much, but the intention was the keep external electical signals away from the coil... 


As the ESP32 sends it's data directly to an InfluxDB Instance, we can read out the values via Grafana very easily. Just make sure the timespan is not too big, as we have about 12k values each minute.


On Idle:
![Screenshot Grafana Idle](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/4991bee5-6ed6-4cd9-a93a-ab5f225548f5)

Walking by:
![Screenshot_20240114_132549](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/33719a76-61e5-41f8-af99-46222f758a2a)


Zoomed in:
![Screenshot_20240114_132555](https://github.com/GeneralGresi/ESP32-Seismometer/assets/59047588/92add889-c608-4dca-8a33-d1ff52c32af0)

As you can see, the dampening can be improved still, however, I consider this a great success.
