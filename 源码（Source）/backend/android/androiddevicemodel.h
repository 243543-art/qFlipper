#pragma once

// [ANDROID-FLASHER] 新增文件：安卓设备列表模型（供 QML ListView 使用）

#include <QAbstractListModel>
#include <QVector>

#include "androidtypes.h"

namespace AndroidFlasher {

class AndroidDeviceModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        PortRole = Qt::UserRole + 1,
        ProductRole,
        ModeNameRole,
        ModeKeyRole,
        SerialRole,
        VidPidRole,
        PathRole,
        DetailRole,
        CanFastbootRole,
        CanEdlRole,
        MtpRole,
        ModeNameEnRole
    };

    explicit AndroidDeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDevices(const QVector<DeviceInfo> &devices);
    void updateProduct(const QString &path, const QString &product, const QString &serial);

    const QVector<DeviceInfo> &devices() const;
    const DeviceInfo *deviceAt(int row) const;
    const DeviceInfo *deviceByPath(const QString &path) const;
    int indexOfPath(const QString &path) const;

private:
    QVector<DeviceInfo> m_devices;
};

} // namespace AndroidFlasher
