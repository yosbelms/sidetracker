
#ifndef INTERFACE_CONFIGEDITOR_CONFIGMENU_H_
#define INTERFACE_CONFIGEDITOR_CONFIGMENU_H_

#include "configMenuDefs.h"
#include "util_math.h"

#include <cstdio>
#include <vector>

class cMenuGroup;
class cMenuItem;

class cMenuBase
{
  protected:
    cMenuBase(menu_t menu_type)
      : type(menu_type)
    {
    }
    menu_t type;

    friend cMenuGroup;
    friend cMenuItem;
};

typedef cMenuBase* hMenuItem;

class cMenuGroup : public cMenuBase
{
  public:
    cMenuGroup(cMenuGroup& parent, const char* name)
      : cMenuBase(menuTypeGroup)
      , parentMenu(&parent)
      , groupName(name)
      , selectedItem(0)
      , slot(0)
    {
        if (&parent != this)
        {
            slot = parent.addChild(this, name);
        }
    }

    ~cMenuGroup() = default;

    cMenuGroup* getSelChild() { return (cMenuGroup*)childs[selectedItem]; }

    uint8_t getCount() const { return static_cast<uint8_t>(childs.size()); }

    char** getNames() { return childsNames.data(); }

    char** getValues()
    {
        reloadValues();
        return childsValues.data();
    }

    uint8_t getSelectedItem() { return selectedItem; }

    uint8_t addChild(hMenuItem child, const char* name)
    {
        uint8_t nextSlot = static_cast<uint8_t>(childs.size());
        childs.push_back(child);
        childsNames.push_back(const_cast<char*>(name));
        childsValues.push_back(nullptr);
        return nextSlot;
    }

    void reloadValues();

    void executeItem();

    int getSlot() { return slot; }

  private:
    cMenuGroup* parentMenu;
    std::vector<hMenuItem> childs;
    std::vector<char*> childsNames;
    std::vector<char*> childsValues;
    const char* groupName;

    uint8_t selectedItem;
    uint8_t slot;

    friend class cMenuItem;
    friend class cConfigEditor;
};

class cMenuItem : public cMenuBase
{
  public:
    cMenuItem(cMenuGroup& parent,
              const char* name,
              const char* desctription,
              menu_item_t type,
              const void* setupPtr)
      : cMenuBase(menuTypeItem)
      , itemName(name)
      , itemDesc(desctription)
      , itemType(type)
      , itemSetup(setupPtr)
      , ptrValueText(itemType == menuItemTypeValueU8 || itemType == menuTypeItemListValues
                       ? new char[kTempValueTextLen + 1]
                       : nullptr)
    {
        parent.addChild(this, name);
        valueEditState = 0;

        // safe check
        switch (itemType)
        {
            case menuTypeItemListText:
            {
                *((strItemTypeListText*)itemSetup)->value =
                  clamp<uint8_t>(*((strItemTypeListText*)itemSetup)->value,
                                 0,
                                 (((strItemTypeListText*)itemSetup)->count) - 1);
                break;
            }
            case menuTypeItemListTextWithAction:
            {
                *((strItemTypeListTextWithAction*)itemSetup)->value =
                  clamp<uint8_t>(*((strItemTypeListTextWithAction*)itemSetup)->value,
                                 0,
                                 (((strItemTypeListTextWithAction*)itemSetup)->count) - 1);
                break;
            }
            case menuTypeItemLabel:
            {
                if (((strItemTypeLabel*)itemSetup)->text == nullptr)
                {
                    ((strItemTypeLabel*)itemSetup)->text = ptrEmptyName;
                }
                break;
            }
            case menuTypeItemLabelPtr:
            {
                if (((strItemTypeLabelPtr*)itemSetup)->text == nullptr)
                {
                    ((strItemTypeLabelPtr*)itemSetup)->text = (const char**)&ptrEmptyName;
                }
                break;
            }
            default: break;
        }
    }

    ~cMenuItem() {}

    // Saved values are loaded after construction, and asset lists can shrink
    // when the card changes. Displaying a value must not index an absent item.
    template<class Setup>
    static char* listValueText(const Setup* setup)
    {
        if (setup == nullptr || setup->value == nullptr || setup->ptrText == nullptr ||
            *setup->value >= setup->count)
        {
            return const_cast<char*>(ptrEmptyName);
        }
        const char* text = setup->ptrText[*setup->value];
        return const_cast<char*>(text != nullptr ? text : ptrEmptyName);
    }

    char* getValue()
    {
        switch (itemType)
        {
            case menuItemTypeValueU8:
            {
                snprintf(ptrValueText,
                         kTempValueTextLen,
                         "%d",
                         *((strItemTypeValue8*)itemSetup)->value +
                           ((strItemTypeValue8*)itemSetup)->dev);
                return ptrValueText;
            }
            case menuTypeItemListValues:
            {
                snprintf(ptrValueText,
                         kTempValueTextLen,
                         "%d",
                         *((strItemTypeValue8*)itemSetup)->value +
                           ((strItemTypeValue8*)itemSetup)->dev);
                return ptrValueText;
            }
            case menuTypeItemListText:
                return listValueText(static_cast<const strItemTypeListText*>(itemSetup));
            case menuTypeItemListTextWithAction:
                return listValueText(static_cast<const strItemTypeListTextWithAction*>(itemSetup));
            case menuTypeItemLabel: return (char*)((strItemTypeLabel*)itemSetup)->text;
            case menuTypeItemLabelPtr: return (char*)*(((strItemTypeLabelPtr*)itemSetup)->text);

            case menuItemTypeEmpty:
            case menuTypeItemActionButton: return (char*)ptrEmptyName;
        }
        return (char*)ptrEmptyName;
    }

    menu_item_t getItemType() { return itemType; }

    const void* getItemSetup() { return itemSetup; }

    uint8_t getValueEditState() { return valueEditState; }

    void resetValueEditState() { valueEditState = 0; }

  private:
    const char* itemName;
    const char* itemDesc;
    menu_item_t itemType;
    const void* itemSetup;

    static constexpr uint8_t kTempValueTextLen = 5;
    char* ptrValueText;

    uint8_t valueEditState;

    friend class cMenuGroup;
};

#endif /* INTERFACE_CONFIGEDITOR_CONFIGMENU_H_ */
