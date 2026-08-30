import {fireEvent, render} from "@testing-library/react-native"
import type {ReactNode} from "react"

import SelectControllerScreen from "@/app/pairing/select-controller"
import {useNavigationStore} from "@/stores/navigation"

jest.mock("expo-router", () => ({
  useFocusEffect: jest.fn(),
}))

jest.mock("@/../../cloud/packages/types/src", () => ({
  DeviceTypes: {
    LIVE: "Mentra Live",
    G1: "Even Realities G1",
    G2: "Even Realities G2",
  },
  ControllerTypes: {
    R1: "Even Realities R1",
    KEYFOB: "XIAO Keyfob",
  },
}))

jest.mock("@/stores/navigation", () => ({
  useNavigationStore: {getState: jest.fn()},
}))

jest.mock("@/contexts/ThemeContext", () => ({
  useAppTheme: () => ({theme: {colors: {text: "#000"}, spacing: {s4: 16}}}),
}))

jest.mock("@/utils/getGlassesImage", () => ({
  getGlassesImage: jest.fn(() => 1),
}))

jest.mock("@/components/ignite", () => {
  const {Text: RNText, View} = require("react-native")
  return {
    Header: () => <View />,
    Text: ({text}: {text?: string}) => <RNText>{text}</RNText>,
  }
})

jest.mock("@/components/ignite/Screen", () => {
  const {View} = require("react-native")
  return {Screen: ({children}: {children: ReactNode}) => <View>{children}</View>}
})

jest.mock("@/components/ui/GlassView", () => {
  const {View} = require("react-native")
  function MockGlassView({children}: {children: ReactNode}) {
    return <View>{children}</View>
  }
  return MockGlassView
})

jest.mock("@/components/ui/Spacer", () => ({
  Spacer: () => null,
}))

jest.mock("@/components/brands/EvenRealitiesLogo", () => ({EvenRealitiesLogo: () => null}))
jest.mock("@/components/brands/MentraLogo", () => ({MentraLogo: () => null}))
jest.mock("@/components/brands/MentraLogoStandalone", () => ({MentraLogoStandalone: () => null}))
jest.mock("@/components/brands/VuzixLogo", () => ({VuzixLogo: () => null}))

describe("controller model selection", () => {
  const push = jest.fn()
  const goBack = jest.fn()

  beforeEach(() => {
    jest.clearAllMocks()
    ;(useNavigationStore.getState as jest.Mock).mockReturnValue({push, goBack})
  })

  it("lists the XIAO Keyfob next to the R1 without super mode", () => {
    const {getByText, getByTestId} = render(<SelectControllerScreen />)

    expect(getByText("XIAO Keyfob")).toBeTruthy()
    expect(getByText("Seeed Studio (unofficial)")).toBeTruthy()
    expect(getByTestId("pairing-model-xiao_keyfob")).toBeTruthy()
    expect(getByTestId("pairing-model-evenrealities_r1")).toBeTruthy()
  })

  it("opens the controller prep flow for the XIAO Keyfob", () => {
    const {getByTestId} = render(<SelectControllerScreen />)

    fireEvent.press(getByTestId("pairing-model-xiao_keyfob"))

    expect(push).toHaveBeenCalledWith("/pairing/prep-controller", {deviceModel: "XIAO Keyfob"})
  })
})
