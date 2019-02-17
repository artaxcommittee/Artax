// Copyright (c) 2014-2016 The Dash Developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2017-2018 The Artax developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "merchantnodelist.h"
#include "ui_merchantnodelist.h"

#include "activemerchantnode.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "init.h"
#include "merchantnode-sync.h"
#include "merchantnodeconfig.h"
#include "merchantnodeman.h"
#include "sync.h"
#include "wallet.h"
#include "walletmodel.h"
#include "askpassphrasedialog.h"

#include <QMessageBox>
#include <QTimer>

CCriticalSection cs_merchantnodes;

MerchantnodeList::MerchantnodeList(QWidget* parent) : QWidget(parent),
                                                  ui(new Ui::MerchantnodeList),
                                                  clientModel(0),
                                                  walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyMerchantnodes->setAlternatingRowColors(true);
    ui->tableWidgetMyMerchantnodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyMerchantnodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyMerchantnodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyMerchantnodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyMerchantnodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyMerchantnodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetMyMerchantnodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyMerchantnodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    // Fill MN list
    fFilterUpdated = true;
    nTimeFilterUpdated = GetTime();
}

MerchantnodeList::~MerchantnodeList()
{
    delete ui;
}

void MerchantnodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
}

void MerchantnodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void MerchantnodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMyMerchantnodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void MerchantnodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH (CMerchantnodeConfig::CMerchantnodeEntry mne, merchantnodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias) {
            std::string strError;
            CMerchantnodeBroadcast mnb;

            bool fSuccess = CMerchantnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if (fSuccess) {
                strStatusHtml += "<br>Successfully started merchantnode.";
                mnodeman.UpdateMerchantnodeList(mnb);
                mnb.Relay();
            } else {
                strStatusHtml += "<br>Failed to start merchantnode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void MerchantnodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH (CMerchantnodeConfig::CMerchantnodeEntry mne, merchantnodeConfig.getEntries()) {
        std::string strError;
        CMerchantnodeBroadcast mnb;

        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CMerchantnode* pmn = mnodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = CMerchantnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if (fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateMerchantnodeList(mnb);
            mnb.Relay();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d merchantnodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void MerchantnodeList::updateMyMerchantnodeInfo(QString strAlias, QString strAddr, CMerchantnode* pmn)
{
    LOCK(cs_mnlistupdate);
    bool fOldRowFound = false;
    int nNewRow = 0;

    for (int i = 0; i < ui->tableWidgetMyMerchantnodes->rowCount(); i++) {
        if (ui->tableWidgetMyMerchantnodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyMerchantnodes->rowCount();
        ui->tableWidgetMyMerchantnodes->insertRow(nNewRow);
    }

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->GetStatus() : "MISSING"));
    GUIUtil::DHMSTableWidgetItem* activeSecondsItem = new GUIUtil::DHMSTableWidgetItem(pmn ? (pmn->lastPing.sigTime - pmn->sigTime) : 0);
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? pmn->lastPing.sigTime : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMyMerchantnodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyMerchantnodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyMerchantnodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyMerchantnodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyMerchantnodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyMerchantnodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyMerchantnodes->setItem(nNewRow, 6, pubkeyItem);
}

void MerchantnodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my merchantnode list only once in MY_MASTERNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_MASTERNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMyMerchantnodes->setSortingEnabled(false);
    BOOST_FOREACH (CMerchantnodeConfig::CMerchantnodeEntry mne, merchantnodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CMerchantnode* pmn = mnodeman.Find(txin);
        updateMyMerchantnodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), pmn);
    }
    ui->tableWidgetMyMerchantnodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void MerchantnodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyMerchantnodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyMerchantnodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm merchantnode start"),
        tr("Are you sure you want to start merchantnode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void MerchantnodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all merchantnodes start"),
        tr("Are you sure you want to start ALL merchantnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void MerchantnodeList::on_startMissingButton_clicked()
{
    if (!merchantnodeSync.IsMerchantnodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until merchantnode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing merchantnodes start"),
        tr("Are you sure you want to start MISSING merchantnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full));

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void MerchantnodeList::on_tableWidgetMyMerchantnodes_itemSelectionChanged()
{
    if (ui->tableWidgetMyMerchantnodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void MerchantnodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
