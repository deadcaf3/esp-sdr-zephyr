.. _esp_sdr_thread:

thread
######

Thread end device on the software radio

Overview
********

An OpenThread minimal Thread device (MTD) on the ESP32-S3, with the ``esp-sdr``
802.15.4 software radio (``CONFIG_ESP_SDR_IEEE802154``, ``doc/ieee802154.md``)
as its radio: no 802.15.4 chip. It joins an existing Thread network from that
network's active dataset. Plan, status and findings: ``doc/thread.md``.

The radio is found through the ``esp-sdr,ieee802154`` devicetree node, chosen as
``zephyr,ieee802154`` in ``boards/xiao_esp32s3_procpu.overlay``. Receive is the
gapless ring on CPU 1, as in ``wpan``.

RSSI and CCA come from the Wi-Fi AGC's input level, uncalibrated
(``doc/ieee802154.md``). Not done yet: OpenThread's own settings in flash (the
dataset is entered again after a reset). The network key is kept in the PSA ITS
store in the storage partition.

Building and flashing
*********************

.. code-block:: console

   west build --sysbuild -b xiao_esp32s3/esp32s3/procpu esp-sdr-zephyr/apps/thread
   west flash          # first install
   west dfu            # later updates over USB

Joining a network
*****************

On the CDC-ACM shell, with the active dataset of the network as hex TLVs:

.. code-block:: none

   uart:~$ ot dataset set active <hex>
   uart:~$ ot ifconfig up
   uart:~$ ot thread start
   uart:~$ ot state
   child

Every transmission goes on the air, with an unvalidated CCA threshold and an
uncalibrated TX power: keep sessions short on a network in use.
