import React from "react";
import { DropdownMenu } from "radix-ui";
import { Ellipsis } from "lucide-react";
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
