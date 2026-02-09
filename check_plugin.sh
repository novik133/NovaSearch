#!/bin/bash

echo "=== NovaSearch Plugin Troubleshooting ==="
echo ""

echo "1. Checking if files are installed:"
echo "   Desktop file:"
ls -la /usr/share/xfce4/panel/plugins/novasearch-panel.desktop && echo "   ✓ Found" || echo "   ✗ Missing"
echo ""
echo "   Plugin library:"
ls -la /usr/lib/xfce4/panel/plugins/novasearch-panel.so && echo "   ✓ Found" || echo "   ✗ Missing"
echo ""

echo "2. Checking desktop file content:"
cat /usr/share/xfce4/panel/plugins/novasearch-panel.desktop
echo ""

echo "3. Checking if plugin library is valid:"
file /usr/lib/xfce4/panel/plugins/novasearch-panel.so
echo ""

echo "4. Checking for required symbol:"
nm -D /usr/lib/xfce4/panel/plugins/novasearch-panel.so | grep xfce_panel_module_construct && echo "   ✓ Symbol found" || echo "   ✗ Symbol missing"
echo ""

echo "5. Checking dependencies:"
ldd /usr/lib/xfce4/panel/plugins/novasearch-panel.so | grep "not found" && echo "   ✗ Missing dependencies" || echo "   ✓ All dependencies found"
echo ""

echo "6. Comparing with working plugin:"
echo "   Other XFCE plugins:"
ls /usr/share/xfce4/panel/plugins/ | head -5
echo ""

echo "7. Checking XFCE panel version:"
xfce4-panel --version
echo ""

echo "=== To add the plugin ==="
echo "1. Right-click on XFCE panel"
echo "2. Select 'Panel' → 'Add New Items'"
echo "3. Look for 'NovaSearch' in the list"
echo "4. Click 'Add'"
echo ""
echo "If NovaSearch doesn't appear in the list, try:"
echo "  xfce4-panel --restart"
echo ""
