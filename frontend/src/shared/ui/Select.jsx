import {
  Children,
  Fragment,
  isValidElement,
  useCallback,
  useState,
} from "react";
import * as Primitive from "@radix-ui/react-select";
import { Check, ChevronDown, ChevronUp } from "lucide-react";
import "./Select.css";

function optionsFrom(children) {
  const options = [];
  Children.forEach(children, (child) => {
    if (!isValidElement(child)) return;
    if (child.type === "option")
      options.push({
        value: String(child.props.value ?? child.props.children),
        label: child.props.children,
        disabled: child.props.disabled,
      });
    else if (child.type === Fragment)
      options.push(...optionsFrom(child.props.children));
  });
  return options;
}

// Keep the form/onChange contract shared by the existing settings and admin
// forms. Radix owns focus, keyboard/typeahead, dismissal and the native form value.
export default function Select({
  children,
  value,
  defaultValue,
  onChange,
  name,
  disabled,
  required,
  className = "",
  ...props
}) {
  const options = optionsFrom(children);
  const [container, setContainer] = useState(null);
  const [input, setInput] = useState("pointer");
  const anchor = useCallback((element) => {
    if (element) setContainer(element.closest("dialog") || document.body);
  }, []);
  return (
    <Primitive.Root
      name={name}
      value={value === undefined ? undefined : String(value)}
      defaultValue={
        defaultValue === undefined ? options[0]?.value : String(defaultValue)
      }
      disabled={disabled}
      required={required}
      onValueChange={(next) =>
        onChange?.({
          target: { value: next, name },
          currentTarget: { value: next, name },
        })
      }
    >
      <Primitive.Trigger
        {...props}
        ref={anchor}
        className={`ui-select ${className}`}
        onPointerDownCapture={() => setInput("pointer")}
        onKeyDownCapture={() => setInput("keyboard")}
      >
        <Primitive.Value />
        <Primitive.Icon className="ui-select-chevron">
          <ChevronDown size={15} />
        </Primitive.Icon>
      </Primitive.Trigger>
      <Primitive.Portal container={container || undefined}>
        <Primitive.Content
          className="ui-select-content"
          data-motion={input}
          position="popper"
          align="end"
          sideOffset={6}
          collisionPadding={12}
          onKeyDownCapture={() => setInput("keyboard")}
        >
          <Primitive.ScrollUpButton className="ui-select-scroll">
            <ChevronUp size={14} />
          </Primitive.ScrollUpButton>
          <Primitive.Viewport className="ui-select-options">
            {options.map((option) => (
              <Primitive.Item
                key={option.value}
                value={option.value}
                disabled={option.disabled}
                className="ui-select-option"
              >
                <Primitive.ItemText>{option.label}</Primitive.ItemText>
                <Primitive.ItemIndicator className="ui-select-check">
                  <Check size={15} />
                </Primitive.ItemIndicator>
              </Primitive.Item>
            ))}
          </Primitive.Viewport>
          <Primitive.ScrollDownButton className="ui-select-scroll">
            <ChevronDown size={14} />
          </Primitive.ScrollDownButton>
        </Primitive.Content>
      </Primitive.Portal>
    </Primitive.Root>
  );
}
