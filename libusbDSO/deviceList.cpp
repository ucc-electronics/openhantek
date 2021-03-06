#include "deviceList.h"
#include <libusb-1.0/libusb.h>
#include <iostream>

#include "deviceDescriptionEntry.h"

namespace DSO {

DeviceList::DeviceList() {
    libusb_init(&_usb_context);
}

DeviceList::~DeviceList() {
    setAutoUpdate(false);
    libusb_exit(_usb_context);
}

void DeviceList::registerModel(const DSODeviceDescription& model) {
    _registeredModels.push_back(model);
    _modelsChanged();
}

int hotplug_callback_fn(libusb_context *ctx, libusb_device *device, libusb_hotplug_event event, void *user_data) {
    if (event==LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED)
        ((DeviceList*)user_data)->hotplugAdd(device);
    else
        ((DeviceList*)user_data)->hotplugRemove(device);

    return 0; // if we return 1 this callback is deregistered.
}

void DeviceList::hotplugAdd(libusb_device *device) {
    uint8_t uniqueID = libusb_get_port_number(device);
    // Check if device is connected and known already
    for(auto itr = _deviceList.begin(); itr != _deviceList.end();++itr) {
        if (itr->get()->getUniqueID() == uniqueID) {
            return;
        }
    }

    // Filter device
    libusb_device_descriptor descriptor;
    if(libusb_get_device_descriptor(device, &(descriptor)) < 0)
        return;

    const DSODeviceDescription* foundModel = nullptr;

    for(const DSODeviceDescription& model: _registeredModels) {
        // Check VID and PID
        if(descriptor.idVendor != model.vendorID)
            continue;
        if(descriptor.idProduct != model.productID)
            continue;

        foundModel = &model;
        break;
    }

    if (!foundModel) return;

    std::cout << "Found device at "
              << (unsigned)libusb_get_bus_number(device)
              << " " << (unsigned)libusb_get_device_address(device)
              << " " << foundModel->modelName
              << " " << foundModel->need_firmware << std::endl;

    // Add device
    DeviceBase* d = foundModel->createDevice(device, *foundModel);
    _deviceList.push_back(std::unique_ptr<DeviceBase>(d));
    _listChanged();
}

void DeviceList::hotplugRemove(libusb_device *device) {
    uint8_t uniqueID = libusb_get_port_number(device);
    bool changed = false;
    for(auto itr = _deviceList.begin(); itr != _deviceList.end();) {
        if (itr->get()->getUniqueID() == uniqueID) {
            itr = _deviceList.erase(itr);
            changed = true;
        }
    }

    if (changed)
        _listChanged();
}

void DeviceList::checkForDevices() const
{
    struct timeval t = {0,0};
    libusb_handle_events_timeout_completed(nullptr, &t, nullptr);
}

void DeviceList::setAutoUpdate(bool autoUpdate) {
    // Unregister callback before doing anything else
    if (_autoUpdate && _callback_handle) {
        libusb_hotplug_deregister_callback(nullptr, _callback_handle);
        _callback_handle = 0;
    }
    _autoUpdate = autoUpdate;
    if (_autoUpdate) {
        int err;
        err = libusb_hotplug_register_callback(nullptr,
                                         libusb_hotplug_event(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED|LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
                                         libusb_hotplug_flag(0), // no flags
                                         LIBUSB_HOTPLUG_MATCH_ANY, // vendor
                                         LIBUSB_HOTPLUG_MATCH_ANY, // product
                                         LIBUSB_HOTPLUG_MATCH_ANY, // usb device class
                                         hotplug_callback_fn,
                                         this,
                                         &_callback_handle);

        if (err != LIBUSB_SUCCESS) {
            std::cerr << libusb_error_name(err) << " on register hotplug add" << std::endl;
        }
    }
}

int DeviceList::update() {
    int errorCode = libusb_init(&(_usb_context));
    if(errorCode != LIBUSB_SUCCESS)
        return errorCode;

    libusb_device **deviceList;

    ssize_t deviceCount = libusb_get_device_list(_usb_context, &deviceList);
    if(deviceCount < 0)
        return LIBUSB_ERROR_NO_DEVICE;

    for(ssize_t deviceIterator = 0; deviceIterator < deviceCount; ++deviceIterator) {
        hotplugAdd(deviceList[deviceIterator]);
    }

    /// \todo Remove old devices

    libusb_free_device_list(deviceList, true);
    return errorCode;
}

void DeviceList::addDevice(DeviceBase* device) {
    _deviceList.push_back(std::shared_ptr<DeviceBase>(device));
    _listChanged();
}

std::shared_ptr<DeviceBase> DeviceList::getDeviceByUID(unsigned uid)
{
    for (std::shared_ptr<DSO::DeviceBase>& device: _deviceList) {
        if (device->getUniqueID() == uid)
            return device;
    }
    return nullptr;
}

const std::vector<std::shared_ptr<DeviceBase> >& DeviceList::getList() const {
    return _deviceList;
}

const std::vector<DSODeviceDescription> DeviceList::getKnownModels() const
{
    return _registeredModels;
}

}
