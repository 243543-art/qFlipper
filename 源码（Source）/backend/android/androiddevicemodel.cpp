#include "androiddevicemodel.h"

// [ANDROID-FLASHER] 新增文件实现：设备列表模型

namespace AndroidFlasher {

AndroidDeviceModel::AndroidDeviceModel(QObject *parent):
    QAbstractListModel(parent)
{}

int AndroidDeviceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

QVariant AndroidDeviceModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid() || index.row() < 0 || index.row() >= m_devices.size()) {
        return QVariant();
    }

    const DeviceInfo &device = m_devices.at(index.row());

    switch(role) {
    case PortRole:          return device.port;
    case ProductRole:       return device.product;
    case ModeNameRole:      return modeName(device.mode);
    case ModeKeyRole:       return modeKey(device.mode);
    case SerialRole:        return device.serial;
    case VidPidRole:        return device.vidPid();
    case PathRole:          return device.path;
    case DetailRole:        return device.detail;
    case CanFastbootRole:   return device.canFastboot();
    case CanEdlRole:        return device.canEdl();
    case MtpRole:           return device.mtp;
    case ModeNameEnRole:    return modeNameEn(device.mode);
    default:                return QVariant();
    }
}

QHash<int, QByteArray> AndroidDeviceModel::roleNames() const
{
    QHash<int, QByteArray> names;
    names[PortRole] = "port";
    names[ProductRole] = "product";
    names[ModeNameRole] = "modeName";
    names[ModeKeyRole] = "modeKey";
    names[SerialRole] = "serial";
    names[VidPidRole] = "vidPid";
    names[PathRole] = "path";
    names[DetailRole] = "detail";
    names[CanFastbootRole] = "canFastboot";
    names[CanEdlRole] = "canEdl";
    names[MtpRole] = "mtp";
    names[ModeNameEnRole] = "modeNameEn";

    return names;
}

void AndroidDeviceModel::setDevices(const QVector<DeviceInfo> &devices)
{
    beginResetModel();
    m_devices = devices;
    endResetModel();
}

void AndroidDeviceModel::updateProduct(const QString &path, const QString &product, const QString &serial)
{
    for(int i = 0; i < m_devices.size(); ++i) {
        if(m_devices.at(i).path != path) {
            continue;
        }

        if(!product.isEmpty()) {
            m_devices[i].product = product;
        }

        if(!serial.isEmpty()) {
            m_devices[i].serial = serial;
        }

        const QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, {ProductRole, SerialRole});
        return;
    }
}

const QVector<DeviceInfo> &AndroidDeviceModel::devices() const
{
    return m_devices;
}

const DeviceInfo *AndroidDeviceModel::deviceAt(int row) const
{
    if(row < 0 || row >= m_devices.size()) {
        return nullptr;
    }

    return &m_devices.at(row);
}

const DeviceInfo *AndroidDeviceModel::deviceByPath(const QString &path) const
{
    if(path.isEmpty()) {
        return nullptr;
    }

    const int row = indexOfPath(path);

    return row >= 0 ? &m_devices.at(row) : nullptr;
}

int AndroidDeviceModel::indexOfPath(const QString &path) const
{
    for(int i = 0; i < m_devices.size(); ++i) {
        if(m_devices.at(i).key() == path) {
            return i;
        }
    }

    return -1;
}

} // namespace AndroidFlasher
