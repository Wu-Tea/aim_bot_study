import React from "react";
import { DropdownMenu } from "radix-ui";
import { Ellipsis } from "lucide-react";
// The native select still owns values, labels, events and keyboard navigation.
// An explicit selected-content box lets long labels ellipsize beside the arrow.
export function Select({ children, ...props }) {
  return <select {...props}><button type="button"><selectedcontent /></button>{children}</select>;
}
export function IconButton({ label, children, ...props }) {
  return (
    <button aria-label={label} title={label} {...props}>
      {children}
    </button>
  );
}
export function Menu({ items, disabled }) {
  return (
    <DropdownMenu.Root>
      <DropdownMenu.Trigger asChild>
        <IconButton
          className="icon-button"
          label="更多操作"
          disabled={disabled}
        >
          <Ellipsis size={19} />
        </IconButton>
      </DropdownMenu.Trigger>
      <DropdownMenu.Portal>
        <DropdownMenu.Content
          className="popup-menu"
          sideOffset={8}
          collisionPadding={12}
        >
          {items.map((item, i) =>
            item ? (
              <DropdownMenu.Item
                key={i}
                className={"menu-item " + (item.danger ? "danger" : "")}
                disabled={item.disabled}
                onSelect={item.action}
              >
                {item.label}
              </DropdownMenu.Item>
            ) : (
              <DropdownMenu.Separator key={i} className="menu-separator" />
            ),
          )}
        </DropdownMenu.Content>
      </DropdownMenu.Portal>
    </DropdownMenu.Root>
  );
}
