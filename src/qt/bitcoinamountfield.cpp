﻿// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bitcoinamountfield.h"

#include "bitcoinunits.h"
#include "guiconstants.h"
#include "qvaluecombobox.h"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>

/** QSpinBox that uses fixed-point numbers internally and uses our own
 * formatting/parsing functions.
 */
class AmountSpinBox: public QAbstractSpinBox
{
    Q_OBJECT

public:
    explicit AmountSpinBox(QWidget *parent):
        QAbstractSpinBox(parent),
        currentUnit(BitcoinUnits::SAFE),
        singleStep(100000), // satoshis
        fAssets(false),
        nAssetDecimals(0)
    {
        setAlignment(Qt::AlignRight);

        connect(lineEdit(), SIGNAL(textEdited(QString)), this, SIGNAL(valueChanged()));
    }

    QValidator::State validate(QString &text, int &pos) const
    {
        if(text.isEmpty())
            return QValidator::Intermediate;
        bool valid = false;
        parse(text, &valid);
        /* Make sure we return Intermediate so that fixup() is called on defocus */
        return valid ? QValidator::Intermediate : QValidator::Invalid;
    }

    void updateAssetUnit(bool fAssets, int nAssetDecimals)
    {
        this->fAssets = fAssets;
        this->nAssetDecimals = nAssetDecimals;
    }

    void fixup(QString &input) const
    {
        bool valid = false;
        CAmount val = parse(input, &valid);
        if(valid)
        {
            if(fAssets)
                input = BitcoinUnits::format(nAssetDecimals, val, false, BitcoinUnits::separatorAlways,true);
            else
                input = BitcoinUnits::format(currentUnit, val, false, BitcoinUnits::separatorAlways);
            lineEdit()->setText(input);
        }
    }

    CAmount value(bool *valid_out=0) const
    {
        return parse(text(), valid_out);
    }

    QString textValue()const
    {
        return text().trimmed();
    }

    void setValue(const CAmount& value)
    {
        if(fAssets)
            lineEdit()->setText(BitcoinUnits::format(nAssetDecimals, value, false, BitcoinUnits::separatorAlways,true));
        else
            lineEdit()->setText(BitcoinUnits::format(currentUnit, value, false, BitcoinUnits::separatorAlways));
        Q_EMIT valueChanged();
    }

    void stepBy(int steps)
    {
        bool valid = false;
        CAmount val = value(&valid);
        val = val + steps * singleStep;
        if(fAssets)
            val = qMin(qMax(val, CAmount(0)), BitcoinUnits::maxAssets());
        else
            val = qMin(qMax(val, CAmount(0)), BitcoinUnits::maxMoney());
        setValue(val);
    }

    void setDisplayUnit(int unit)
    {
        bool valid = false;
        CAmount val = value(&valid);

        currentUnit = unit;

        if(valid)
            setValue(val);
        else
            clear();
    }

    void setSingleStep(const CAmount& step)
    {
        singleStep = step;
    }

    QSize minimumSizeHint() const
    {
        if(cachedMinimumSizeHint.isEmpty())
        {
            ensurePolished();

            const QFontMetrics fm(fontMetrics());
            int h = lineEdit()->minimumSizeHint().height();
            int w = 0;
            if(fAssets)
                w = fm.width(BitcoinUnits::format(BitcoinUnits::SAFE, BitcoinUnits::maxAssets(), false, BitcoinUnits::separatorAlways,true));
            else
                w = fm.width(BitcoinUnits::format(BitcoinUnits::SAFE, BitcoinUnits::maxMoney(), false, BitcoinUnits::separatorAlways));
            w += 2; // cursor blinking space

            QStyleOptionSpinBox opt;
            initStyleOption(&opt);
            QSize hint(w, h);
            QSize extra(35, 6);
            opt.rect.setSize(hint + extra);
            extra += hint - style()->subControlRect(QStyle::CC_SpinBox, &opt,
                                                    QStyle::SC_SpinBoxEditField, this).size();
            // get closer to final result by repeating the calculation
            opt.rect.setSize(hint + extra);
            extra += hint - style()->subControlRect(QStyle::CC_SpinBox, &opt,
                                                    QStyle::SC_SpinBoxEditField, this).size();
            hint += extra;
            hint.setHeight(h);

            opt.rect = rect();

            cachedMinimumSizeHint = style()->sizeFromContents(QStyle::CT_SpinBox, &opt, hint, this)
                                    .expandedTo(QApplication::globalStrut());
        }
        return cachedMinimumSizeHint;
    }

private:
    int currentUnit;
    CAmount singleStep;
    mutable QSize cachedMinimumSizeHint;
    bool fAssets;
    int nAssetDecimals;

    /**
     * Parse a string into a number of base monetary units and
     * return validity.
     * @note Must return 0 if !valid.
     */
    CAmount parse(const QString &text, bool *valid_out=0) const
    {
        CAmount val = 0;
        bool valid = false;
        if(fAssets)
            valid = BitcoinUnits::parse(nAssetDecimals, text, &val,fAssets);
        else
            valid = BitcoinUnits::parse(currentUnit, text, &val);
        if(valid)
        {
            if(fAssets)
            {
                if(val < 0 || val > BitcoinUnits::maxAssets())
                    valid = false;
            }else
            {
                if(val < 0 || val > BitcoinUnits::maxMoney())
                    valid = false;
            }
        }
        if(valid_out)
            *valid_out = valid;
        return valid ? val : 0;
    }

protected:
    bool event(QEvent *event)
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Comma)
            {
                // Translate a comma into a period
                QKeyEvent periodKeyEvent(event->type(), Qt::Key_Period, keyEvent->modifiers(), ".", keyEvent->isAutoRepeat(), keyEvent->count());
                return QAbstractSpinBox::event(&periodKeyEvent);
            }
        }
        return QAbstractSpinBox::event(event);
    }

    StepEnabled stepEnabled() const
    {
        if (isReadOnly()) // Disable steps when AmountSpinBox is read-only
            return StepNone;
        if (text().isEmpty()) // Allow step-up with empty field
            return StepUpEnabled;

        StepEnabled rv = 0;
        bool valid = false;
        CAmount val = value(&valid);
        if(valid)
        {
            if(val > 0)
                rv |= StepDownEnabled;
            if(fAssets)
            {
                if(val < BitcoinUnits::maxAssets())
                    rv |= StepUpEnabled;
            }else
            {
                if(val < BitcoinUnits::maxMoney())
                    rv |= StepUpEnabled;
            }
        }
        return rv;
    }

Q_SIGNALS:
    void valueChanged();
};

#include "bitcoinamountfield.moc"

BitcoinAmountField::BitcoinAmountField(QWidget *parent) :
    QWidget(parent),
    amount(0),
    fAssets(false),
    nAssetDecimals(0)
{
    amount = new AmountSpinBox(this);
    amount->setLocale(QLocale::c());
    amount->installEventFilter(this);
    amount->setMaximumWidth(170);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->addWidget(amount);
    unit = new QValueComboBox(this);
    unit->setModel(new BitcoinUnits(this));
    label = new QLabel(this);
    layout->addWidget(unit);
    layout->addWidget(label);
    layout->addStretch(1);
    layout->setContentsMargins(0,0,0,0);
    label->setVisible(false);

    setLayout(layout);

    setFocusPolicy(Qt::TabFocus);
    setFocusProxy(amount);

    // If one if the widgets changes, the combined content changes as well
    connect(amount, SIGNAL(valueChanged()), this, SIGNAL(valueChanged()));
    connect(unit, SIGNAL(currentIndexChanged(int)), this, SLOT(unitChanged(int)));

    // Set default based on configuration
    unitChanged(unit->currentIndex());
}

void BitcoinAmountField::clear()
{
    amount->clear();
    unit->setCurrentIndex(0);
}

void BitcoinAmountField::setEnabled(bool fEnabled)
{
    amount->setEnabled(fEnabled);
    unit->setEnabled(fEnabled);
}

void BitcoinAmountField::updateAssetUnit(const QString &unitName, bool fAssets, int nAssetDecimals)
{
    unit->setVisible(!fAssets);
    label->setText(unitName);
    label->setVisible(fAssets);
    this->fAssets = fAssets;
    this->nAssetDecimals = nAssetDecimals;
    amount->updateAssetUnit(fAssets,nAssetDecimals);
    amount->clear();
}

bool BitcoinAmountField::validate()
{
    bool valid = false;
    value(&valid);
    setValid(valid);
    return valid;
}

void BitcoinAmountField::setValid(bool valid)
{
    if (valid)
        amount->setStyleSheet("");
    else
        amount->setStyleSheet(STYLE_INVALID);
}

bool BitcoinAmountField::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        // Clear invalid flag on focus
        setValid(true);
    }
    return QWidget::eventFilter(object, event);
}

QWidget *BitcoinAmountField::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, amount);
    QWidget::setTabOrder(amount, unit);
    return unit;
}

CAmount BitcoinAmountField::value(bool *valid_out) const
{
    return amount->value(valid_out);
}

QString BitcoinAmountField::textValue()const
{
    QString str = amount->textValue().trimmed();
    QStringList list = str.split(" ");

	return list.join("");
}

void BitcoinAmountField::setValue(const CAmount& value)
{
    amount->setValue(value);
}

void BitcoinAmountField::setReadOnly(bool fReadOnly)
{
    amount->setReadOnly(fReadOnly);
}

void BitcoinAmountField::unitChanged(int idx)
{
    // Use description tooltip for current unit for the combobox
    unit->setToolTip(unit->itemData(idx, Qt::ToolTipRole).toString());

    // Determine new unit ID
    int newUnit = unit->itemData(idx, BitcoinUnits::UnitRole).toInt();

    amount->setDisplayUnit(newUnit);
}

void BitcoinAmountField::setDisplayUnit(int newUnit)
{
    unit->setValue(newUnit);
}

void BitcoinAmountField::setSingleStep(const CAmount& step)
{
    amount->setSingleStep(step);
}
