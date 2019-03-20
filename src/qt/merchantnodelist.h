// Copyright (c) 2014-2016 The Dash Developers
// Copyright (c) 2016-2017 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERCHANTNODELIST_H
#define MERCHANTNODELIST_H

#include "merchantnode.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_MERCHANTNODELIST_UPDATE_SECONDS 60
#define MERCHANTNODELIST_UPDATE_SECONDS 15
#define MERCHANTNODELIST_FILTER_COOLDOWN_SECONDS 3

namespace Ui
{
class MerchantnodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Merchantnode Manager page widget */
class MerchantnodeList : public QWidget
{
    Q_OBJECT

public:
    explicit MerchantnodeList(QWidget* parent = 0);
    ~MerchantnodeList();

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu* contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyMerchantnodeInfo(QString strAlias, QString strAddr, CMerchantnode* pmn);
    void updateMyNodeList(bool fForce = false);

Q_SIGNALS:

private:
    QTimer* timer;
    Ui::MerchantnodeList* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;
    CCriticalSection cs_mnlistupdate;
    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint&);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyMerchantnodes_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // MERCHANTNODELIST_H
