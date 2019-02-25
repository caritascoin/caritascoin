#ifndef CORALNODELIST_H
#define CORALNODELIST_H

#include "coralnode.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_CORALNODELIST_UPDATE_SECONDS 60
#define CORALNODELIST_UPDATE_SECONDS 15
#define CORALNODELIST_FILTER_COOLDOWN_SECONDS 3

namespace Ui
{
class CoralnodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** coralnode Manager page widget */
class CoralnodeList : public QWidget
{
    Q_OBJECT

public:
    explicit CoralnodeList(QWidget* parent = 0);
    ~CoralnodeList();

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu* contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyCoralnodeInfo(QString strAlias, QString strAddr, CCoralnode* pmn);
    void updateMyNodeList(bool fForce = false);

Q_SIGNALS:

private:
    QTimer* timer;
    Ui::CoralnodeList* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;
    CCriticalSection cs_mnlistupdate;
    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint&);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyCoralnodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // CORALNODELIST_H
