import React from "react";
import { Popover } from "radix-ui";
import { ChevronRight, X } from "lucide-react";
import { fmt } from "./bridge";
export function Field({ field, value, onChange }) {
  const id = "f-" + field.path,
    scale = field.scale;
  const numeric = field.kind === "float" || field.kind === "int";
  const valid = numeric
    ? typeof value === "number" &&
      Number.isFinite(value) &&
      value >= field.limits[0] &&
      value <= field.limits[1] &&
      (field.kind !== "int" || Number.isInteger(value))
    : true;
  return (
    <div className={"row field-row " + (!valid ? "invalid" : "")}>
      <div className="field-label">
        <label htmlFor={id}>{field.label}</label>
        {field.help && (
          <Popover.Root>
            <Popover.Trigger asChild>
              <button
                type="button"
                className="help"
                aria-label={"说明：" + field.label}
              >
                ?
              </button>
            </Popover.Trigger>
            <Popover.Portal>
              <Popover.Content
                className="field-help"
                sideOffset={8}
                collisionPadding={16}
                aria-label={field.label + "说明"}
              >
                <div className="field-help-heading">
                  <strong>{field.label}</strong>
                  <Popover.Close aria-label="关闭说明">
                    <X size={15} />
                  </Popover.Close>
                </div>
                <p>{field.help}</p>
              </Popover.Content>
            </Popover.Portal>
          </Popover.Root>
        )}
        {field.hot_reload === false && <small>重启生效</small>}
      </div>
      {field.kind === "bool" ? (
        <input
          id={id}
          type="checkbox"
          className="switch"
          checked={!!value}
          onChange={(e) => onChange(e.target.checked)}
        />
      ) : field.choices ? (
        <select
          id={id}
          value={value}
          onChange={(e) => onChange(e.target.value)}
        >
          {Object.entries(field.choices).map(([k, v]) => (
            <option key={k} value={k}>
              {v}
            </option>
          ))}
        </select>
      ) : numeric ? (
        <div className="rowcontrol">
          {field.unit === "%" && !field.readonly && (
            <input
              type="range"
              aria-label={field.label + "滑块"}
              min={field.limits[0] * scale}
              max={field.limits[1] * scale}
              step="0.1"
              value={
                typeof value === "number"
                  ? value * scale
                  : field.limits[0] * scale
              }
              onChange={(e) => onChange(Number(e.target.value) / scale)}
            />
          )}
          <span className="number">
            <input
              id={id}
              type="number"
              readOnly={field.readonly}
              aria-invalid={!valid}
              min={field.limits[0] * scale}
              max={field.limits[1] * scale}
              step={field.kind === "int" ? 1 : "any"}
              value={typeof value === "number" ? fmt(value * scale) : value}
              onChange={(e) =>
                onChange(
                  e.target.value === "" ? "" : Number(e.target.value) / scale,
                )
              }
            />
            <span>{field.unit}</span>
          </span>
        </div>
      ) : (
        <input
          id={id}
          className="text-field"
          readOnly={field.readonly}
          value={value ?? ""}
          onChange={(e) => onChange(e.target.value)}
        />
      )}
      {!valid && (
        <small className="field-error">
          {field.limits[0] * scale}–{field.limits[1] * scale} {field.unit}
          {field.kind === "int" ? "，请输入整数" : ""}
        </small>
      )}
    </div>
  );
}
export function Group({ name, fields, draft, setValue, fold = false }) {
  const content = fields.map((f) => (
    <Field
      key={f.path}
      field={f}
      value={draft.values[f.path]}
      onChange={(v) => setValue(f.path, v)}
    />
  ));
  return fold ? (
    <details className="disclosure">
      <summary>
        <span className="disclosure-name">{name}</span>
        <span className="disclosure-count">{fields.length} 项</span>
        <ChevronRight className="disclosure-chevron" size={18} />
      </summary>
      <div className="inside">{content}</div>
    </details>
  ) : (
    <section className="section">
      <div className="sectionhead">
        <h2>{name}</h2>
      </div>
      {content}
    </section>
  );
}
